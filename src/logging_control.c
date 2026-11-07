#include "logging_control.h"
#include "log.h"
#include "usart_board.h"
#include <string.h>

volatile bool logging_enabled;
static char command[24];
static uint32_t length, rx_errors, rx_drops;
static bool discard_line, at_boundary, dump_active;
static const char *reply;

static void reset_line(bool discard) {
  length = 0U;
  discard_line = discard;
}

void logging_control_init(void) {
  logging_enabled = false;
  reset_line(false);
  at_boundary = true;
  dump_active = false;
  reply = NULL;
  rx_errors = uart2.rx_error_count;
  rx_drops = uart2.rx_drop_count;
}

static bool send_dump(const uint8_t *data, uint32_t size) {
  return usart_write(&uart2, data, size);
}

static bool rx_fault(void) {
  return rx_errors != uart2.rx_error_count || rx_drops != uart2.rx_drop_count;
}

static void execute_command(void) {
  if (strcmp(command, "BEGIN_LOG") == 0) {
    reply = logger_start() ? "LOGGING_ENABLED\r\n" : "BEGIN_LOG REFUSED\r\n";
  } else if (strcmp(command, "LOG_END") == 0) {
    reply = logger_finish() ? logger_status_reply() : "LOG_END REFUSED\r\n";
  } else if (strcmp(command, "LOG_CLEAR") == 0) {
    reply = logger_request_clear() ? "LOG CLEARING\r\n" : "LOG_CLEAR REFUSED\r\n";
  } else if (strcmp(command, "LOG_DUMP") == 0) {
    dump_active = logger_request_dump();
    if (!dump_active) reply = "LOG_DUMP REFUSED\r\n";
  } else if (strcmp(command, "LOG_STATUS") == 0) {
    reply = logger_status_reply();
  }
}

void logging_control_poll(void) {
  logging_enabled = logger_is_recording();
  if (dump_active) {
    /* Binary stream owns USART2. GNSS and LoRa continue in the main loop.
     * Ignore commands sent during the stream so they cannot erase the log. */
    uint8_t byte;
    for (uint32_t budget = BUFFER_SIZE; budget && usart_read(&uart2, &byte, 1U);
         --budget) {
      at_boundary = byte == '\r' || byte == '\n';
    }
    if (logger_dump_poll(send_dump)) {
      dump_active = false;
      reset_line(!at_boundary);
    }
    rx_errors = uart2.rx_error_count;
    rx_drops = uart2.rx_drop_count;
    return;
  }
  if (reply != NULL) {
    if (!usart_write(&uart2, (const uint8_t *)reply, (uint32_t)strlen(reply)))
      return;
    reply = NULL;
  }

  bool fault = rx_fault();
  if (fault) { reset_line(true); at_boundary = false; }
  bool complete = false;
  uint8_t byte;
  for (uint32_t budget = BUFFER_SIZE; budget != 0U; --budget) {
    if (!usart_read(&uart2, &byte, 1U)) break;
    if (byte == '\r' || byte == '\n') {
      at_boundary = true;
      if (!discard_line && length != 0U) {
        command[length] = '\0';
        complete = true;
        break; /* Process commands in order, including BEGIN_LOG then LOG_END. */
      }
      reset_line(false);
    } else {
      at_boundary = false;
      if (byte == 0U || length == sizeof(command) - 1U) discard_line = true;
      if (!discard_line) command[length++] = (char)byte;
    }
  }
  if (fault || rx_fault()) {
    complete = false;
    reset_line(!at_boundary);
  }
  rx_errors = uart2.rx_error_count;
  rx_drops = uart2.rx_drop_count;
  if (complete) {
    execute_command();
    reset_line(false);
  }
  logging_enabled = logger_is_recording();
  if (reply != NULL &&
      usart_write(&uart2, (const uint8_t *)reply, (uint32_t)strlen(reply)))
    reply = NULL;
}
