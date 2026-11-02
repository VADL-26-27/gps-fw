#include "logging_control.h"
#include "usart_board.h"

volatile bool logging_enabled;
static const uint8_t begin_command[] = "BEGIN_LOG";
static const uint8_t enabled_reply[] = "LOGGING_ENABLED\r\n";
static uint32_t matched;
static uint32_t rx_errors;
static uint32_t rx_drops;
static bool discard_line;
static bool at_line_boundary;
static bool reply_pending;

static void reset_command(bool discard) {
  matched = 0U;
  discard_line = discard;
}

void logging_control_init(void) {
  logging_enabled = false;
  reset_command(false);
  at_line_boundary = true;
  reply_pending = false;
  rx_errors = uart2.rx_error_count;
  rx_drops = uart2.rx_drop_count;
}

static bool receive_byte(uint8_t byte) {
  if (byte == '\r' || byte == '\n') {
    bool complete = !discard_line && matched == sizeof(begin_command) - 1U;
    reset_command(false);
    at_line_boundary = true;
    return complete;
  }
  at_line_boundary = false;
  if (!discard_line) {
    if (matched >= sizeof(begin_command) - 1U || byte != begin_command[matched]) {
      discard_line = true;
    } else {
      ++matched;
    }
  }
  return false;
}

void logging_control_poll(void) {
  bool fault = rx_errors != uart2.rx_error_count || rx_drops != uart2.rx_drop_count;
  if (fault) {
    reset_command(true);
    at_line_boundary = false;
  }
  bool begin_seen = false;
  uint8_t byte;
  for (uint32_t budget = BUFFER_SIZE; budget != 0U; --budget) {
    if (!usart_read(&uart2, &byte, 1U)) {
      break;
    }
    if (receive_byte(byte)) {
      begin_seen = true;
    }
  }
  if (fault || rx_errors != uart2.rx_error_count || rx_drops != uart2.rx_drop_count) {
    /* Do not arm from a batch with transport corruption. Keep an existing
     * session armed, and discard any incomplete line before parsing again.
     */
    begin_seen = false;
    reset_command(!at_line_boundary);
  }
  rx_errors = uart2.rx_error_count;
  rx_drops = uart2.rx_drop_count;
  if (begin_seen) {
    logging_enabled = true;
    reply_pending = true; /* Repeated commands are harmless and re-acknowledged. */
  }
  if (reply_pending &&
      usart_write(&uart2, enabled_reply, sizeof(enabled_reply) - 1U)) {
    reply_pending = false;
  }
}
