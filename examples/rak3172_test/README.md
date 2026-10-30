# STM32 Arduino RAK3172 test

Open `rak3172_test.ino` as its own sketch in Arduino IDE using the
**STM32 MCU based boards** package by STMicroelectronics, version **3.x**
(the installed version is 3.0.0; this uses its `Uart` class). Do not add the
bare-metal project's `src/` or `inc/` files to this sketch.

## Board and upload

- Board: **Generic STM32F4 series**.
- Board part number: **Generic F413CGUx**, if the PCB actually has an STM32F413CGU.
  Otherwise select the exact chip/package marked on your MCU.
- USB support: **None**. The sketch overrides the clock to internal HSI at 16 MHz.
- Upload method: **STM32CubeProgrammer (SWD)** with your ST-Link.
  STM32CubeProgrammer must be installed for this upload method.
- Stop any VS Code debug session before uploading from Arduino IDE.

Upload programs this sketch in place of the current bare-metal firmware.
The sketch requires no external libraries beyond the STM32 Arduino core.

## PCB UART connections

| STM32 pin | Connection |
| --- | --- |
| PB13, UART5 TX | RAK AT UART RX |
| PB12, UART5 RX | RAK AT UART TX |
| PA2, USART2 TX | Optional 3.3 V USB-UART adapter RX for logs |
| PA3, USART2 RX | Reserved console input; not required for this test |
| GND | Common ground with the adapter |

The log console is **USART2 at 115200 baud**, not native USB. ST-Link programming
does not automatically provide console output; its virtual COM RX must be
physically routed to PA2, or a separate 3.3 V USB-UART adapter is needed.
Only attach the adapter to accessible console pads already suitable for this
connection; avoid driving signals already driven by another PCB component.
If PA2 is routed to another peripheral, change the `Console` pins to an available
UART pair on the PCB. No console connection is required for the sketch to run.

## Behavior

The sketch retries `AT` every five seconds until the module answers. It then
selects LoRa P2P, disables radio reception, configures 915 MHz / SF7 / BW125 /
CR4/5 / preamble 8 / 14 dBm, and sends `Hello from STM32` every five seconds
after each send attempt completes. Set `P2P_SETTINGS` for your region/module
and matching receiver settings. Set `RAK_BAUD` to the RAK's actual saved baud
rate (default 115200 for RUI3 AT firmware).

Successful sends print `OK`, `+EVT:TXP2P DONE`, and `[TX confirmed] count=...`.
That confirms local transmission, not remote reception. Timeouts print diagnostics
and retry without entering a permanent WFI loop. The RAK must run compatible
RUI3 AT firmware and have its normal power, reset, boot, and antenna connections.
RAK reset/boot pins are not controlled by this sketch because the PCB routing
has not been provided.

Without a console, inspect these globals using a debugger and the Arduino
build's ELF (not the bare-metal project's `build/firmware.elf`):

- `rak_rx_bytes`
- `rak_rx_raw_count`, `rak_rx_raw`
- `rak_last_command`, `rak_last_response`
- `rak_tx_confirmed_count`, `rak_timeout_count`, `rak_error_count`
