# Nose Cone GPS & Telemetry Module — Firmware

Firmware for the nosecone GNSS + telemetry subsystem (VADL 2026–2027 season). Built around an STM32F411RET6, a u-blox MAX-M10S GNSS receiver, and a RAK3172 LoRa telemetry module — **each with its own dedicated antenna**, no shared RF path.

Design Source: [Hardware Design Document](https://vanderbilt365-my.sharepoint.com/:w:/r/personal/evan_s_ticknor_vanderbilt_edu/_layouts/15/Doc.aspx?sourcedoc=%7BDB7462E8-8C19-4EBC-A119-87AD5BF244CF%7D&file=Design%20Specification%20Group%20Report.docx&action=default&mobileredirect=true)

---

## 1. System Overview

The subsystem provides real-time position, velocity, and timing data during ground testing and flight, and downlinks it via LoRa telemetry. GPS and LoRa each have their own independent 50 Ω RF path and antenna connector — there is no shared antenna, no RF switch, and no antenna arbitration logic required in firmware.

**Core hardware:**

| Component | Part | Interface to MCU |
|---|---|---|
| MCU | STM32F411RET6 (LQFP-64, up to 100 MHz) | — |
| GNSS receiver | u-blox MAX-M10S | UART, NMEA 0183 |
| Telemetry radio | RAK3172 (LoRa) | UART |
| GPS antenna | Dedicated SMA port (JG) | RF, independent path |
| LoRa antenna | Dedicated SMA port (J1) | RF, independent path |
| GPS SAW filter | SAFFB1G56KB0F0AR1X | Passive (in GPS RF path only) |
| Storage | microSD | SDIO (4-bit) |
| Debug | SWD + UART1 | Telemetry debug header |
| Power | NiMH 4.8V 2000mAh → 3.3V LDO | — |

## 2. RTOS

Three tasks run concurrently: GPS UART data streams in continuously and can't be dropped, LoRa telemetry transmits on its own schedule, and SD logging has to keep up with both. Because GPS and LoRa have independent antennas, **there is no handover sequence and no timing-critical antenna arbitration**. GPS reception and LoRa transmission can run fully concurrently without one blocking or interrupting the other.

Current RTOS choice: FreeRTOS (update if another proves necessary)

No microsecond-scale guard timing is required for antenna control. TIM-driven precision timing may still be relevant for other purposes (e.g. GPS_TIMEPULSE / 1PPS handling, see §5), but not for RF path switching.

## 3. Task Architecture

| Task | Responsibility | Priority (suggested) |
|---|---|---|
| `gps_task` | Read MAX-M10S UART (DMA + idle-line IRQ), parse NMEA, publish fix data | High |
| `telem_task` | Own the RAK3172 UART, LoRa scheduling and transmission | Medium-high |
| `sd_log_task` | Buffer and write GPS/telemetry data to microSD over SDIO | Medium |
| `housekeeping_task` | Power LED, watchdog kick, fault monitoring | Low |

Inter-task communication: queue for GPS fix data → `sd_log_task`; queue or direct call for telemetry data → `telem_task`. No shared-resource arbitration needed between `gps_task` and `telem_task` since they don't contend for RF hardware.

Bare-metal register-level access is preferred over HAL for the UART DMA setup and GPIO handling, though with the antenna handover removed, the case for bare-metal is now driven primarily by DMA/idle-IRQ throughput needs rather than microsecond timing constraints.

`USB_Task` (CDC debug console + data offload, bench-only) is intentionally not in this table, no queue link to any flight-critical task, kept isolated so a USB hang can't propagate into a reset (SYS-REQ-02). DFU flashing bypasses the RTOS entirely (BOOT0 held at reset drops straight into the ROM bootloader before `main()` runs), so it has no interaction with `USB_Task` or task priorities.

## 4. Interface / Pin Map (from ICD + SDD)

| Connector | Signal(s) | Type | Notes |
|---|---|---|---|
| JP1 — Power In | VBAT, GND | Power | 4.8–5V DC (NiMH pack) |
| JU1 — USB-C | +5V_USB, USB_D±, GND | USB | External power, CDC data/debug console, and DFU flashing (via BOOT0 held at reset) — see §4.1 |
| DH1 — Telemetry Debug Header | TELEM_SWCLK, TELEM_SWDIO, TELEM_UART1_RX, TELEM_UART1_TX | SWD + 3.3V UART | Debug and telemetry passthrough |
| JG — SMA Port | GPS_ANT | RF Signal | Dedicated GPS antenna, NMEA 0183 receive path to MAX-M10S |
| J1 — SMA Port | ANT_1 | RF Signal | Dedicated LoRa antenna, transmit path to RAK3172 |
| microSD | SD_CLK, SD_CMD, SD_DAT0–3, VDD_MCU, GND | SDIO (4-bit) | Data logging. Connector: Molex 503398-1892 (push-push, SMT, 8-position, with card-detect switch). **Note: SD_CD is not currently listed in the ICD/SDD — confirm with hardware whether the connector's detect switch is actually routed to an MCU pin before relying on it in `sd_log_task`.** |
| System | BOOT0 | Digital in | Held high (via button) at reset to enter USB DFU bootloader (see §4.1). Physically accessible via a dedicated button — confirmed. Default pull state should rest low; confirm with hardware lead. |
| Telemetry (RAK3172) | TELEM_UART2_TX (PA9), TELEM_UART2_RX (PA10), TELEM_BOOT0 (PC0), TELEM_RST (PC1) | 3.3V UART, Digital | **`TELEM_BOOT0`/`TELEM_RST` are the RAK3172 module's own boot/reset control pins — distinct from the MCU's `BOOT0` used for DFU.** Don't conflate the two. |
| GPS | GPS_UART_TX (PC7), GPS_UART_RX (PC6), GPS_TIMEPULSE (PA5), GPS_RESET_1 (PA8) | 3.3V UART, Time pulse, Digital | `GPS_TIMEPULSE` is the MAX-M10S's 1PPS output — useful for precise time sync, not currently used by any task above. Worth deciding whether a task or ISR should consume it. |

### 4.1 USB Roles

JU1 (USB-C, native OTG_FS on PA11/PA12) serves three purposes:

1. **Power** — bench/external 5V in.
2. **Data / normal STM comms** — CDC-class debug console and telemetry data offload. Primary intended use during bench testing and HIL.
3. **DFU flashing** — the STM32F411's built-in USB bootloader is reachable by holding `BOOT0` at reset. Functional, but **ST-Link via DH1 (SWD) is the default flash path** — no reason to give up a working, faster flash workflow. DFU is a fallback for situations without an ST-Link on hand (e.g. field firmware updates).

**Future consideration:** USB endpoints not in active use for CDC comms can potentially be reassigned for bulk memory offload (e.g. dumping SD-logged data post-flight over USB rather than pulling the card). Not in scope for V1.0 firmware.

## 5. Power Budget (design targets)

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

## 6. Requirements Traceability (subset relevant to firmware)

| Req ID | Requirement | Firmware relevance |
|---|---|---|
| SYS-REQ-01 | RF Transparency & Downlink, ≥12 dB link margin | Applies independently to each antenna path now — no cross-antenna interference to manage in firmware |
| SYS-REQ-02 | Vibration resilience, no MCU resets | Watchdog task, brown-out handling |

## 7. Repo Structure (proposed)
```
/src
/drivers
usart_gps.c/.h # MAX-M10S UART + DMA, NMEA framing
usart_lora.c/.h # RAK3172 UART
sdio.c/.h # microSD driver
/tasks
gps_task.c
telem_task.c
sd_log_task.c
housekeeping_task.c
/rtos
FreeRTOSConfig.h
main.c
/test
hil/ # Hardware-in-the-loop test scripts
/docs
design_spec.docx # source design doc (this README's basis)
```
## 8. Open Items From the Design Doc

- Mounting holes, RF shielding, thermal dissipation: flagged as "layout stage" — not yet finalized, no firmware dependency yet.
- SD card connector confirmed: Molex 503398-1892 (push-push, 8-position, with card-detect switch). The actual microSD card part (capacity, speed class) is still TBD — max SDIO clock, write latency, and buffering/queue sizing in `sd_log_task` depend on that choice once made. **SD_CD routing to the MCU is unconfirmed — not present in the ICD/SDD tables.**
- Test point definitions and HIL coverage plan are placeholders in the source doc — need to be filled in before verification can start (SYS-REQ-02 vibration testing depends on this).
- BOOT0 physical accessibility confirmed — dedicated button. Default pull state (should rest low) still needs confirmation from hardware lead.
- `GPS_TIMEPULSE` (1PPS) is wired but not currently assigned to any task — decide if/how firmware should consume it for time sync.

---
