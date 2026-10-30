#include "hardware.h"
#include "timer.h"
#include "usart_board.h"
#include "spi_board.h"
#include <string.h>

/* RUI3 AT firmware. UART5: PB13 TX, PB12 RX, 115200 8N1.
 * USART2 console: PA2 TX, PA3 RX, 115200 8N1.
 * Radio P2P: 915 MHz, SF7, BW125, CR4/5, preamble 8, 14 dBm.
 */
#define RAK_P2P_COMMAND "AT+P2P=915000000:7:125:0:8:14\r\n"
#define TRANSMIT_INTERVAL_MS 5000U

volatile uint32_t rak_attempt_count; /* Radio send attempts. */
volatile uint32_t rak_ok_count;      /* Commands acknowledged with OK. */
volatile uint32_t rak_tx_confirmed_count;
volatile uint32_t rak_timeout_count;
volatile uint32_t rak_error_count;
volatile bool rak_ok;
const char *volatile rak_last_command;
char rak_last_response[128];
uint8_t rak_rx_raw[128];
volatile uint32_t rak_rx_raw_count;

static void uart_write_all(usart_t *port, const uint8_t *data,
                           uint32_t length) {
  /* All messages below fit in the driver's 128-byte queue. */
  while (!usart_write(port, data, length)) {
    delay_ms(1U);
  }
}

static void console_write(const char *text) {
  uart_write_all(&uart2, (const uint8_t *)text, (uint32_t)strlen(text));
}

static void rak_discard_input(void) {
  uint8_t byte;
  while (usart_read(&uart5, &byte, 1U)) {
  }
}

static void rak_send_command(const char *command) {
  rak_discard_input();
  rak_last_command = command;
  rak_last_response[0] = '\0';
  memset(rak_rx_raw, 0, sizeof(rak_rx_raw));
  rak_rx_raw_count = 0U;
  rak_ok = false;
  console_write(">> ");
  console_write(command);
  uart_write_all(&uart5, (const uint8_t *)command, (uint32_t)strlen(command));
}

static bool rak_wait_reply(const char *expected, uint32_t timeout_ms) {
  char line[128];
  uint32_t length = 0U;
  bool overflow = false;
  uint32_t start = now_ms();

  while ((uint32_t)(now_ms() - start) < timeout_ms) {
    uint8_t byte;
    if (!usart_read(&uart5, &byte, 1U)) {
      continue;
    }
    if (rak_rx_raw_count < sizeof(rak_rx_raw)) {
      rak_rx_raw[rak_rx_raw_count++] = byte;
    }
    if (byte == '\r') {
      continue;
    }
    if (byte != '\n') {
      if (length < sizeof(line) - 1U) {
        line[length++] = (char)byte;
      } else {
        overflow = true;
      }
      continue;
    }

    line[length] = '\0';
    bool valid = !overflow && length != 0U;
    length = 0U;
    overflow = false;
    if (!valid) {
      continue;
    }
    strcpy(rak_last_response, line);
    console_write("<< ");
    console_write(line);
    console_write("\r\n");

    if (strstr(line, "ERROR") != NULL) {
      ++rak_error_count;
      return false;
    }
    if (strcmp(line, expected) == 0) {
      if (strcmp(expected, "OK") == 0) {
        rak_ok = true;
        ++rak_ok_count;
      }
      return true;
    }
  }

  ++rak_timeout_count;
  console_write("Timeout waiting for ");
  console_write(expected);
  console_write("\r\n");
  return false;
}

static bool rak_command_ok(const char *command) {
  rak_send_command(command);
  return rak_wait_reply("OK", 2000U);
}

static bool rak_configure(void) {
  if (!rak_command_ok("AT\r\n")) {
    return false;
  }
  if (!rak_command_ok("AT+NWM=0\r\n")) {
    return false;
  }

  /* A mode change can reboot the RAK. Drain its startup text while waiting. */
  uint32_t start = now_ms();
  while ((uint32_t)(now_ms() - start) < 3000U) {
    rak_discard_input();
  }

  return rak_command_ok("AT\r\n") && rak_command_ok("AT+PRECV=0\r\n") &&
         rak_command_ok(RAK_P2P_COMMAND);
}

bool configured = false;
int main(void) {
  hardwareInit();
  board_usart_init();
  board_spi_init();

  console_write("RAK3172 P2P transmitter starting\r\n");
  delay_ms(3000U);

  /* Hex-encoded ASCII payload: "Hello STM32". */
  static const char transmit_command[] = "AT+PSEND=48656C6C6F2053544D3332\r\n";
  bool configured = false;

  for (;;) {
    if (!configured) {
      configured = rak_configure();
      if (!configured) {
        console_write("Setup failed; retrying in 5 seconds\r\n");
        delay_ms(5000U);
        continue;
      }
      console_write("P2P ready\r\n");
    }

    ++rak_attempt_count;
    if (rak_command_ok(transmit_command) &&
        rak_wait_reply("+EVT:TXP2P DONE", 5000U)) {
      ++rak_tx_confirmed_count;
      console_write("TX confirmed: Hello STM32\r\n");
    } else {
      console_write("TX unconfirmed; rechecking setup\r\n");
      configured = false;
    }
    delay_ms(TRANSMIT_INTERVAL_MS);
  }

  // // Q43 GNSS Module has default output rate of 10 Hz, we will use that
  // while (1) {
  //   if (!configured) {
  //     configured = rak_configure();
  //   }

  //   spi_poll(&spi1);
  //   flash_poll(&flash);
  // }
}
