#include "hardware.h"
#include "timer.h"
#include "usart_board.h"
#include "spi_board.h"
#include "gps.h"
#include "gps_stream.h"
#include "lora.h"
#include "telemetry.h"
#include "logging_control.h"
#include "log.h"

/* RUI3 AT firmware. UART5: PB13 TX, PB12 RX, 115200 8N1.
 * USART2 console: PA2 TX, PA3 RX, 115200 8N1.
 * Radio P2P: 915 MHz, SF12, BW125, CR4/8, preamble 8, 22 dBm.
 */
gps_stream_t gnss;
gps_transmit_t rfPacket;
gps_mailbox_t currFix;
gps_mailbox_t
    logPacket; /* Stable 1 Hz snapshot until the next prepared record. */
volatile uint32_t gnss_uart_fault_count;
volatile uint32_t
    logging_sample_count; /* Successfully queued snapshots; not flash writes. */
static uint32_t gnss_rx_errors;
static uint32_t gnss_rx_drops;

static bool gnss_uart_fault(void) {
  if (gnss_rx_errors == uart1.rx_error_count &&
      gnss_rx_drops == uart1.rx_drop_count) {
    return false;
  }
  gnss_rx_errors = uart1.rx_error_count;
  gnss_rx_drops = uart1.rx_drop_count;
  ++gnss_uart_fault_count;
  gps_stream_reset(&gnss);
  return true;
}

static void poll_gnss(void) {
  uint8_t byte;
  bool discard = gnss_uart_fault();
  /* A bounded amount of work lets radio replies and SPI continue running. */
  for (uint32_t budget = BUFFER_SIZE; budget != 0U; --budget) {
    if (!usart_read(&uart1, &byte, 1U)) {
      break;
    }
    if (!discard) {
      gps_stream_feed(&gnss, byte, now_ms(), &currFix);
    }
  }
  /* An error arising during this batch also invalidates its candidate fix. */
  gnss_uart_fault();
}

int main(void) {
  hardwareInit();
  board_usart_init();
  logging_control_init();
  board_spi_init();
  logger_init(&flash);
  gps_stream_init(&gnss);
  lora_init();

  // Q43 GNSS Module has default output rate of 10 Hz, we will use that
  while (1) {
    spi_poll(&spi1);
    flash_poll(&flash);
    logger_poll();
    logging_control_poll();
    lora_poll();

    poll_gnss();

    if (timer2_take_tick() &&
        telemetry_tick(&gnss, &currFix, now_ms(), &logPacket, &rfPacket)) {
      if (logging_enabled && logger_enqueue(&logPacket)) {
        ++logging_sample_count;
      }
    }
  }
}
