#include "logging_control.h"
#include "log.h"
#include "usart_board.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

usart_t uart2;
static uint8_t received[2048];
static uint32_t read_pos, read_length, reply_count;
static bool allow_reply = true, error_on_last_byte;
static bool recording, ready = true, finishing;
static uint32_t clears, dumps;
static char last_reply[64];
bool logger_start(void) { if (!ready && !recording) return false; recording = true; return true; }
bool logger_finish(void) { if (!recording) return false; recording = false; finishing = true; return true; }
bool logger_is_recording(void) { return recording; }
bool logger_request_clear(void) { if (recording || finishing) return false; ++clears; return true; }
bool logger_request_dump(void) { if (recording || finishing) return false; ++dumps; return true; }
bool logger_dump_poll(bool (*send)(const uint8_t *, uint32_t)) {
  return send((const uint8_t *)"E", 1);
}
const char *logger_status_reply(void) { return "LOG READY\r\n"; }
static void reset(void) {
  recording = finishing = false;
  logging_control_init();
}

bool usart_read(usart_t *port, uint8_t *data, uint32_t length) {
  assert(port == &uart2 && length == 1U);
  if (read_pos == read_length) return false;
  *data = received[read_pos++];
  if (error_on_last_byte && read_pos == read_length) {
    ++uart2.rx_error_count;
    error_on_last_byte = false;
  }
  return true;
}
bool usart_write(usart_t *port, const uint8_t *data, uint32_t length) {
  assert(port == &uart2);
  if (!allow_reply) return false;
  assert(length < sizeof(last_reply));
  memcpy(last_reply, data, length); last_reply[length] = 0;
  ++reply_count;
  return true;
}
static void send_bytes(const uint8_t *bytes, uint32_t length) {
  assert(read_pos == read_length && length <= sizeof(received));
  memcpy(received, bytes, length);
  read_pos = 0;
  read_length = length;
  logging_control_poll();
  while (read_pos < read_length) logging_control_poll();
}
static void send_line(const char *line) {
  send_bytes((const uint8_t *)line, (uint32_t)strlen(line));
}

int main(void) {
  reset();
  assert(!logging_enabled);
  logging_control_poll();
  assert(!logging_enabled && reply_count == 0);

  const char *invalid[] = {"begin_log\n", "XBEGIN_LOG\n", "BEGIN_LOGX\n",
                          " BEGIN_LOG\n", "BEGIN_LOG \n", "STOP_LOG\n"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    send_line(invalid[i]);
    assert(!logging_enabled && reply_count == 0);
  }
  uint8_t embedded_zero[] = {'B','E','G','I','N','_','L','O','G',0,'\n'};
  send_bytes(embedded_zero, sizeof(embedded_zero));
  assert(!logging_enabled);
  uint8_t overlong[1200];
  memset(overlong, 'X', sizeof(overlong));
  send_bytes(overlong, sizeof(overlong));
  logging_control_poll(); /* Continue after the bounded first batch. */
  send_line("BEGIN_LOG\n");
  assert(!logging_enabled); /* Still part of the overlong line. */

  send_line("BEG");
  assert(!logging_enabled);
  send_line("IN_LOG");
  assert(!logging_enabled); /* Delimiter is required. */
  send_line("\r\n");
  assert(logging_enabled && reply_count == 1);
  send_line("BEGIN_LOG\n");
  assert(logging_enabled && reply_count == 2);
  send_line("STOP_LOG\nGARBAGE\n");
  assert(logging_enabled && reply_count == 2); /* No stop command. */

  reset(); /* Simulate reset/power cycle: state is not saved. */
  assert(!logging_enabled);
  send_line("BEGIN_LOG\r");
  assert(logging_enabled && reply_count == 3);
  reset();
  assert(!logging_enabled);

  allow_reply = false;
  send_line("BEGIN_LOG\n");
  assert(logging_enabled && reply_count == 3);
  allow_reply = true;
  logging_control_poll();
  assert(reply_count == 4); /* Retry a full console TX queue without blocking. */

  reset();
  error_on_last_byte = true;
  send_line("BEGIN_LOG\n");
  assert(!logging_enabled && reply_count == 4);
  send_line("BEGIN_LOG\n");
  assert(logging_enabled && reply_count == 5);
  ++uart2.rx_drop_count;
  send_line("STOP_LOG\n");
  assert(logging_enabled); /* A UART fault cannot disarm an existing session. */

  reset();
  send_line("BEGIN_");
  ++uart2.rx_drop_count;
  send_line("LOG\n");
  assert(!logging_enabled);
  send_line("BEGIN_LOG\n");
  assert(logging_enabled);
  send_line("LOG_DUMP\n"); assert(dumps == 0 && logging_enabled);
  send_line("LOG_CLEAR\n"); assert(clears == 0 && logging_enabled);
  send_line("LOG_END\r\n"); assert(!logging_enabled && finishing);
  send_line("LOG_DUMP\n"); assert(dumps == 0);
  send_line("LOG_CLEAR\n"); assert(clears == 0);
  finishing = false; ready = false;
  send_line("BEGIN_LOG\n"); assert(!logging_enabled);
  assert(strcmp(last_reply, "BEGIN_LOG REFUSED\r\n") == 0);
  send_line("LOG_STATUS\n"); assert(strcmp(last_reply, "LOG READY\r\n") == 0);
  send_line("LOG_CLEAR\n"); assert(clears == 1);
  send_line("LOG_DUMP\n"); assert(dumps == 1);
  send_line("LOG_CLEAR\n"); /* Commands during binary dump are discarded. */
  assert(clears == 1 && strcmp(last_reply, "E") == 0);
  ready = true;
  memcpy(received, "BEGIN_LOG\nLOG_END\n", 18);
  read_pos = 0; read_length = 18;
  logging_control_poll();
  assert(logging_enabled);
  logging_control_poll(); assert(!logging_enabled && finishing);
  reset();
  error_on_last_byte = true; send_line("LOG_CLEAR\n"); assert(clears == 1);
  error_on_last_byte = true; send_line("LOG_DUMP\n"); assert(dumps == 1);
  puts("Logging control tests passed: command ordering, begin/end, refusal while busy, "
       "dump exclusivity, reply retry, malformed input, UART faults.");
}
