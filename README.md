# Nose Cone GPS & Telemetry Module — Firmware

Firmware for the nosecone GNSS + telemetry subsystem (VADL 2026–2027 season). Built around an STM32F411RET6, a u-blox MAX-M10S GNSS receiver, and a RAK3172 LoRa telemetry module sharing a single antenna.

Design Source: [Hardware Design Document](https://vanderbilt365-my.sharepoint.com/:w:/r/personal/evan_s_ticknor_vanderbilt_edu/_layouts/15/Doc.aspx?sourcedoc=%7BDB7462E8-8C19-4EBC-A119-87AD5BF244CF%7D&file=Design%20Specification%20Group%20Report.docx&action=default&mobileredirect=true)

---

## 1. System Overview

The subsystem provides real-time position, velocity, and timing data during ground testing and flight, and downlinks it via LoRa telemetry. Because GPS and LoRa share one antenna through an RF switch, firmware is responsible for making sure the two never collide on the RF path.

**Core hardware:**

| Component | Part | Interface to MCU |
|---|---|---|
| MCU | STM32F411RET6 (LQFP-64, up to 100 MHz) | — |
| GNSS receiver | u-blox MAX-M10S | UART, NMEA 0183 |
| Telemetry radio | RAK3172 (LoRa) | UART |
| RF switch | MXD8621C SPDT | Single GPIO (ANT_SEL) |
| GPS SAW filter | SAFFB1G56KB0F0AR1X | Passive (in RF path, no MCU control) |
| Storage | microSD | SDIO (4-bit) |
| Debug | SWD + UART1 | Telemetry debug header |
| Power | NiMH 4.8V 2000mAh → 3.3V LDO | — |

## 2. RTOS

Three tasks have to happen concurrently without blocking each other: GPS UART data streams in continuously and can't be dropped, LoRa telemetry has to transmit on its own schedule, and SD logging has to keep up with both, while a shared-antenna handover (§3) has to interrupt and resume GPS reception cleanly around every LoRa TX burst. 

Current RTOS choice: FREE RTOS (Update if another proves to be necessary)

One caveat: the antenna handover's guard times (10–100 µs, §3) are finer than RTOS tick resolution typically allows (1 ms with a 1 kHz tick). `vTaskDelay()` can't hit a 10 µs wait, as it'll round up to at least one tick. Those specific waits need to be driven by a hardware timer (TIM) interrupt or a tight busy-wait inside a masked critical section, not RTOS scheduling. The RTOS still owns everything around that critical section (deciding when to start a handover, buffering GPS data during it, resuming tasks after), it just doesn't own the microsecond-level waits inside it.

**Note:** the antenna handover critical section (§3) must not span the entire LoRa TX burst — see §3 for the split critical-section approach.

## 3. Antenna Handover Sequence (Timing-Critical)

Driven by MCU before/after every LoRa TX burst:

1. Assert `GPS_LNA_EN` low — cuts antenna from MAX-M10S LNA
2. Wait 50 µs — LNA power-down settle
3. Assert `ANT_SEL` high — switch antenna to LoRa port
4. Wait 10 µs — switch settling
5. Trigger RAK3172 transmission
6. On TX complete, disable RAK3172 PA
7. Wait 50 µs — PA-off guard time
8. Assert `ANT_SEL` low — return antenna to GPS port
9. Wait 10 µs — switch settling
10. Assert `GPS_LNA_EN` high — restore V_ANT to MAX-M10S
11. Wait 100 µs — LNA stabilization before GPS data is considered valid

**Critical section scope — important:** Steps 1–4 and steps 6–9 are µs-scale and belong in a masked critical section or hardware-timer ISR. Step 5 (the actual TX, 50–180 ms per the burst constraint below) must run **outside** that critical section under normal scheduling. If the critical section stays entered through step 5, interrupts — including SysTick — are masked for up to 180 ms, stalling the entire RTOS scheduler and risking a watchdog trip (SYS-REQ-02 depends on avoiding this). Implementation: enter critical section for 1–4, exit before triggering TX, re-enter for 6–9.

**Constraint:** LoRa TX bursts must stay under ~100–200 ms (the GPS tracking-loop coast time) so GPS lock survives the handover without reacquisition. At SF7–SF9, burst durations of 50–180 ms satisfy this.

**RF budget (why the sequence exists at all):** RAK3172 TX at +22 dBm is attenuated by the RF switch (−25 dB), the GPS SAW filter (−40 dB @ 915 MHz), and the MAX-M10S's internal SAW (−20 dB) — 85 dB total, leaving −63 dBm at the LNA input against a −10 dBm damage threshold (53 dB margin). The passive chain is defense-in-depth; **the LNA power-down step is the primary protection mechanism** and must not be skipped or reordered.

## 4. Proposed Task Architecture

| Task | Responsibility | Priority (suggested) |
|---|---|---|
| `gps_task` | Read MAX-M10S UART (DMA + idle-line IRQ), parse NMEA, publish fix data | High |
| `telem_task` | Own the RAK3172 UART, LoRa scheduling, and the antenna handover sequence (§3). Priority is elevated via `vTaskPrioritySet()` immediately before a handover and restored immediately after — not statically highest, to avoid starving `gps_task`/`sd_log_task` outside of handovers. | Medium-high (elevated only during handover) |
| `sd_log_task` | Buffer and write GPS/telemetry data to microSD over SDIO | Medium |
| `housekeeping_task` | Power LED, watchdog kick, fault monitoring | Low |

`GPS_LNA_EN` / `ANT_SEL` are not a separate task — they're owned by `rf_switch.c` and called directly by `telem_task` under a masked critical section for the µs-scale steps only (see §3's critical section note). No dedicated `antenna_arbiter` task; a full task adds scheduling overhead for what's really two GPIOs and a timing FSM.

The µs-scale guard-time waits themselves (§3, steps 2/4/7/9/11) are likewise not a task. Two implementation options, per §2:

- **Hardware TIM interrupt** — a timer configured to fire after the guard interval, with the ISR handling the next step of the handover sequence. Preferred if the sequence needs to be interruptible or if `telem_task` shouldn't sit in a busy-wait.
- **Busy-wait inside the masked critical section** — simpler to implement, but blocks `telem_task` (and, since interrupts are masked, blocks everything) for the duration. Acceptable given the waits are short (10–100 µs) and bounded, but must stay strictly inside the critical-section boundaries defined in §3 — never spanning step 5's TX.

Either way, this logic lives in `rf_switch.c`, is bare-metal register-level (TIM registers or `DWT`/cycle-count busy-wait — not `HAL_Delay()`), and does not appear as an RTOS task.

Inter-task communication: queue for GPS fix data → `sd_log_task`; a binary semaphore/mutex around `rf_switch.c` calls so `gps_task` doesn't attempt a read during a LoRa burst.

Bare-metal register-level access is preferred over HAL for the UART DMA setup, TIM-driven guard timing, and GPIO toggles on `GPS_LNA_EN`/`ANT_SEL` — the microsecond-level guard times in §3 leave little room for HAL abstraction overhead.

`USB_Task` (CDC debug console + data offload, bench-only) is intentionally not in this table — no queue link to any flight-critical task, kept isolated so a USB hang can't propagate into a reset (SYS-REQ-02). DFU flashing bypasses the RTOS entirely (BOOT0 held at reset drops straight into the ROM bootloader before `main()` runs), so it has no interaction with `USB_Task` or task priorities at all.

## 5. Interface / Pin Map (from ICD)

| Connector | Signal(s) | Type | Notes |
|---|---|---|---|
| JP1 — Power In | VBAT, GND | Power | 4.8–5V DC (NiMH pack) |
| JU1 — USB-C | +5V_USB, USB_D±, GND | USB | External power, CDC data/debug console, and DFU flashing (via BOOT0 held at reset) — see §5.1 |
| DH1 — Telemetry Debug Header | SWCLK, SWDIO, UART1_RX, UART1_TX | SWD + 3.3V UART | Debug and telemetry passthrough |
| JA1 — SMA Port | RF_IN_OUT | RF | Shared GPS/LoRa antenna, routed through MXD8621C |
| microSD | SD_CLK, SD_CMD, SD_DAT0–3, SD_CD | SDIO (4-bit) | Data logging. Connector: Molex 503398-1892 (push-push, SMT, 8-position, with card-detect switch). SD_CD wired to the connector's detect switch — firmware should poll/interrupt on this to confirm card presence before mount attempts in `sd_log_task`, rather than assuming a card is always inserted. |
| GPIO | `GPS_LNA_EN`, `ANT_SEL` | Digital out | Antenna arbitration (§3) |
| System | BOOT0 | Digital in | Held high at reset to enter USB DFU bootloader (see §5.1). Must be physically accessible (jumper/button) — confirm placement and default-low pull with hardware lead. |

### 5.1 USB Roles

JU1 (USB-C, native OTG_FS on PA11/PA12) serves three purposes:

1. **Power** — bench/external 5V in.
2. **Data / normal STM comms** — CDC-class debug console and telemetry data offload. Primary intended use during bench testing and HIL.
3. **DFU flashing** — the STM32F411's built-in USB bootloader is reachable by holding `BOOT0` at reset. Functional, but **ST-Link via DH1 (SWD) is the default flash path** — no reason to give up a working, faster flash workflow. DFU is a fallback for situations without an ST-Link on hand (e.g. field firmware updates).

**Future consideration:** USB endpoints not in active use for CDC comms can potentially be reassigned for bulk memory offload (e.g. dumping SD-logged data post-flight over USB rather than pulling the card). Not in scope for V1.0 firmware — flagged here so the driver isn't written in a way that forecloses it later (e.g. avoid hardcoding a single fixed endpoint config if a mass-storage or bulk-transfer class might be added down the line).


## 6. Power Budget (design targets)

| Component | Idle | Peak Active |
|---|---|---|
| MCU (STM32) | 0.5 mA | 37 mA (100 MHz, all peripherals) |
| Telemetry (RAK3172) | 5.22 mA (RX) | 100 mA (TX) |
| GPS (MAX-M10S) | 7 mA (tracking) | 15.3 mA (acquisition) |
| LDO | 0.1 mA | 0.1 mA |
| SD Card | 1–25 mA | 100–200 mA |
| Power LED | 1.2 mA | 1.2 mA |
| **Total** | **39.02 mA** | **353.6 mA** (worst-case SD spike; supply capacity 800 mA) |

Firmware power-mode implications: MCU should drop to STOP mode (~9 µA per trade study) between active GPS/telemetry windows where possible, though the "100% duty cycle" allocation for MCU and GPS in the design doc suggests continuous operation is currently assumed.

Worth revisiting once real duty-cycle/battery-life numbers are needed for the ~6 hour runtime target.

## 7. Requirements Traceability (subset relevant to firmware)

| Req ID | Requirement | Firmware relevance |
|---|---|---|
| SYS-REQ-01 | RF Transparency & Downlink, ≥12 dB link margin | Antenna handover sequence must not degrade GPS lock or LoRa TX power |
| SYS-REQ-02 | Vibration resilience, no MCU resets | Watchdog task, brown-out handling |

## 8. Repo Structure (proposed)

```
/src
  /drivers
    usart_gps.c/.h        # MAX-M10S UART + DMA, NMEA framing
    usart_lora.c/.h        # RAK3172 UART
    rf_switch.c/.h         # ANT_SEL / GPS_LNA_EN control, handover FSM
    sdio.c/.h               # microSD driver
  /tasks
    gps_task.c
    telem_task.c
    sd_log_task.c
    housekeeping_task.c
  /rtos
    FreeRTOSConfig.h
main.c
/test
  hil/                      # Hardware-in-the-loop test scripts
/docs
  design_spec.docx           # source design doc (this README's basis)
```

## 9. Open Items From the Design Doc
- SD card part is still TBD; current/power numbers are approximate.
- Test point definitions and HIL coverage plan are placeholders in the source doc — need to be filled in before verification can start. This blocks SYS-REQ-02 vibration testing, and now also blocks verifying the antenna handover sequence (§3) itself — there's currently no defined way to observe GPS_LNA_EN/ANT_SEL timing or confirm the 10–100 µs guard windows are actually being hit on hardware.

---
