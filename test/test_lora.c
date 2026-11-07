#include "lora.h"
#include "usart_board.h"
#include "telemetry_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

usart_t uart5;
static uint32_t clock_ms, sent_count, rx_read, rx_length;
static uint8_t received[4096];
static char sent[256];
static bool allow_queue = true, error_on_last_byte;

uint32_t now_ms(void) { return clock_ms; }
bool usart_write(usart_t *port, const uint8_t *data, uint32_t len) {
  assert(port == &uart5);
  if (!allow_queue) return false;
  assert(len < sizeof(sent));
  memcpy(sent, data, len);
  sent[len] = '\0';
  ++sent_count;
  return true;
}
bool usart_read(usart_t *port, uint8_t *data, uint32_t len) {
  assert(port == &uart5 && len == 1U);
  if (rx_read == rx_length) return false;
  *data = received[rx_read++];
  if (error_on_last_byte && rx_read == rx_length) {
    ++uart5.rx_error_count;
    error_on_last_byte = false;
  }
  return true;
}
static void reply(const char *text) {
  assert(rx_read == rx_length);
  rx_read = 0U;
  rx_length = (uint32_t)strlen(text);
  assert(rx_length <= sizeof(received));
  memcpy(received, text, rx_length);
  lora_poll();
}
static void advance(uint32_t milliseconds) {
  clock_ms += milliseconds;
  lora_poll();
}
static void ready(bool change_mode) {
  allow_queue = true;
  error_on_last_byte = false;
  rx_read = rx_length = 0U;
  lora_init();
  assert(!lora_is_ready());
  advance(3000U);
  assert(strcmp(sent, "ATZ\r\n") == 0);
  advance(3000U);
  lora_poll();
  assert(strcmp(sent, "AT\r\n") == 0);
  reply("\r\nOK\r\n");
  assert(strcmp(sent, "AT+NWM=?\r\n") == 0);
  if (change_mode) {
    reply("AT+NWM=1\r\nOK\r\n");
    assert(strcmp(sent, "AT+NWM=0\r\n") == 0);
    /* Mode switching can reboot without returning OK. */
    reply("RAKwireless RAK3172\r\nCurrent Work Mode: LoRa P2P.\r\n");
    advance(3000U);
    lora_poll();
    assert(strcmp(sent, "AT+NWM=?\r\n") == 0);
    reply("0\r\nOK\r\n");
  } else {
    reply("AT+NWM=0\r\nOK\r\n");
  }
  assert(strcmp(sent, "AT+PRECV=0\r\n") == 0);
  reply("OK\r\n");
  assert(strcmp(sent, "AT+CAD=0\r\n") == 0);
  reply("OK\r\n");
  assert(strcmp(sent, LORA_P2P_COMMAND) == 0);
  reply("OK\r\n");
  assert(lora_is_ready() && !lora_tx_busy());
}
int main(void) {
  gps_transmit_t packet = {0};
  uint8_t *bytes = (uint8_t *)&packet;
  for (uint32_t i = 0; i < sizeof(packet); ++i) bytes[i] = (uint8_t)i;
  assert(!transmit_packet(&packet));
  assert(!transmit_no_lock());
  ready(true);
  assert(!transmit_packet(NULL));
  uint32_t attempts = rak_attempt_count, confirmed = rak_tx_confirmed_count;
  allow_queue = false;
  assert(!transmit_packet(&packet));
  assert(!transmit_no_lock());
  assert(rak_attempt_count == attempts && rak_tx_status == LORA_TX_IDLE);
  allow_queue = true;
  assert(transmit_packet(&packet));
  assert(strlen(sent) == 99U);
  assert(strcmp(sent, "AT+PSEND=000102030405060708090A0B0C0D0E0F10111213"
                      "1415161718191A1B1C1D1E1F202122232425262728292A2B\r\n") == 0);
  assert(strcmp(rak_last_command, sent) == 0);
  uint32_t sends = sent_count;
  assert(!transmit_packet(&packet) && sent_count == sends);
  assert(!transmit_no_lock() && sent_count == sends);
  reply("\r\nO");
  assert(rak_tx_status == LORA_TX_WAIT_OK);
  reply("K\r\n+EVT:TXP2P DO");
  assert(rak_ok && rak_tx_status == LORA_TX_WAIT_DONE);
  assert(rak_tx_confirmed_count == confirmed);
  reply("NE\r\n");
  assert(rak_tx_status == LORA_TX_CONFIRMED && !lora_tx_busy());
  assert(rak_tx_confirmed_count == confirmed + 1U);
  reply("+EVT:TXP2P DONE\r\nOK\r\n");
  assert(rak_tx_confirmed_count == confirmed + 1U);

  gps_transmit_t fixture = {
    .fix = {.i_tow_ms = 123456700, .latitude_e7 = 361432112,
      .longitude_e7 = -868055040, .altitude_msl_mm = 168730,
      .velocity_north_mm_s = -1234, .velocity_east_mm_s = 5678,
      .velocity_down_mm_s = -9012, .horizontal_accuracy_mm = 700,
      .vertical_accuracy_mm = 1200, .fix_type = 3, .satellites_used = 25,
      .fix_flags = 0x83, .time_valid_flags = 7}, .sequence = 42};
  assert(transmit_packet(&fixture));
  assert(strcmp(sent, "AT+PSEND=" TELEMETRY_FIXTURE_HEX "\r\n") == 0);
  reply("OK\r\n+EVT:TXP2P DONE\r\n");

  attempts = rak_attempt_count; confirmed = rak_tx_confirmed_count;
  assert(transmit_no_lock());
  assert(strcmp(sent, "AT+PSEND=4E4F204C4F434B20464F554E44\r\n") == 0);
  assert(rak_attempt_count == attempts + 1U && lora_tx_busy());
  assert(!transmit_no_lock() && !transmit_packet(&packet));
  reply("OK\r\n");
  assert(rak_tx_status == LORA_TX_WAIT_DONE);
  assert(rak_tx_confirmed_count == confirmed);
  reply("+EVT:TXP2P DONE\r\n");
  assert(rak_tx_confirmed_count == confirmed + 1U && !lora_tx_busy());

  assert(transmit_packet(&packet));
  reply("+EVT:TXP2P DONE\r\n"); /* DONE alone cannot confirm a new command. */
  assert(rak_tx_status == LORA_TX_WAIT_OK);
  reply("OK\r\n+EVT:TXP2P DONE\r\n");
  assert(rak_tx_status == LORA_TX_CONFIRMED);

  assert(transmit_packet(&packet));
  char oversized[256];
  memset(oversized, 'X', 160U);
  strcpy(oversized + 160U, "OK\r\n");
  reply(oversized);
  assert(rak_tx_status == LORA_TX_WAIT_OK);
  reply("OK\r\n");
  assert(rak_tx_status == LORA_TX_WAIT_DONE);
  advance(LORA_TX_TIMEOUT_MS);
  assert(rak_tx_status == LORA_TX_TIMEOUT && !lora_is_ready());
  confirmed = rak_tx_confirmed_count;
  reply("+EVT:TXP2P DONE\r\n");
  assert(rak_tx_confirmed_count == confirmed);
  assert(!transmit_packet(&packet));
  advance(5000U); /* Automatic recovery starts with a real module reset. */
  advance(3000U);
  assert(strcmp(sent, "ATZ\r\n") == 0);

  ready(false);
  clock_ms = UINT32_MAX - 100U;
  assert(transmit_packet(&packet));
  advance(LORA_COMMAND_TIMEOUT_MS);
  assert(rak_tx_status == LORA_TX_TIMEOUT); /* Millisecond wraparound. */

  ready(false);
  assert(transmit_packet(&packet));
  uint32_t errors = rak_error_count;
  reply("AT_BUSY_ERROR\r\n");
  assert(rak_tx_status == LORA_TX_ERROR && rak_error_count == errors + 1U);
  assert(!lora_is_ready());

  ready(false);
  assert(transmit_packet(&packet));
  confirmed = rak_tx_confirmed_count;
  error_on_last_byte = true;
  reply("OK\r\n+EVT:TXP2P DONE\r\n");
  assert(rak_tx_status == LORA_TX_ERROR);
  assert(rak_tx_confirmed_count == confirmed); /* Corrupted batch not confirmed. */

  ready(false);
  assert(transmit_packet(&packet));
  ++uart5.rx_drop_count;
  lora_poll();
  assert(rak_tx_status == LORA_TX_ERROR);

  /* Configuration errors/timeouts stay unready and never send a packet. */
  lora_init();
  advance(3000U);
  advance(3000U);
  lora_poll();
  errors = rak_error_count;
  reply("AT_ERROR\r\n");
  assert(rak_error_count == errors + 1U && !lora_is_ready());
  lora_init();
  advance(3000U);
  advance(3000U);
  lora_poll();
  uint32_t timeouts = rak_timeout_count;
  advance(LORA_COMMAND_TIMEOUT_MS);
  assert(rak_timeout_count == timeouts + 1U && !lora_is_ready());
  puts("LoRa tests passed: configuration, encoding, split replies, busy guard, "
       "confirmation, duplicates, errors, timeouts, wraparound, recovery, "
       "no-lock text encoding/confirmation.");
}
