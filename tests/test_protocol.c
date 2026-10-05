#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rak3172_protocol.h"
#include "telemetry_frame.h"
#include "ground_rx_fifo.h"

static rak3172_result_t parse(const char *s) { return rak3172_parse_result(s, strlen(s)); }
int main(void) {
  char command[544];
  assert(rak3172_build_sync(command, sizeof(command)) == 4);
  assert(strcmp(command, "AT\r\n") == 0);
  assert(rak3172_build_sync(command, 4) == -1);
  assert(rak3172_build_get_mode(command, sizeof(command)) == 10);
  assert(strcmp(command, "AT+NWM=?\r\n") == 0);
  assert(rak3172_build_set_mode(command, sizeof(command), 0) == 10);
  /* Synthetic test settings only; no flight RF configuration is supplied. */
  assert(rak3172_build_set_p2p(command, sizeof(command), 868000000, 7, 0, 0, 65535, 14) > 0);
  assert(strcmp(command, "AT+P2P=868000000:7:0:0:65535:14\r\n") == 0);
  assert(rak3172_build_set_p2p(command, sizeof(command), 0, 7, 0, 0, 8, 14) == -1);
  uint8_t payload[255];
  memset(payload, 0xa5, sizeof(payload));
  assert(rak3172_build_psend_hex(command, sizeof(command), payload, 255) == 521);
  assert(strlen(command) == 521 && memcmp(command, "AT+PSEND=A5", 11) == 0);
  assert(rak3172_build_psend_hex(command, 521, payload, 255) == -1);
  assert(rak3172_build_psend_hex(command, sizeof(command), payload, 256) == -1);
  assert(rak3172_build_psend_hex(command, sizeof(command), payload, 0) == -1);
  assert(parse("OK") == RAK3172_RESULT_OK);
  assert(parse("NOT_OK") == RAK3172_RESULT_UNKNOWN);
  assert(parse("OK\r\nOK") == RAK3172_RESULT_UNKNOWN);
  assert(parse("AT_BUSY_ERROR") == RAK3172_RESULT_ERROR);
  assert(parse("+EVT:TXP2P DONE") == RAK3172_RESULT_TX_DONE);
  assert(parse("+EVT:RXP2P RECEIVE TIMEOUT") == RAK3172_RESULT_RX_TIMEOUT);
  assert(parse("+EVT:RXP2P:-112:-1:AA55") == RAK3172_RESULT_RX_PACKET);
  assert(parse("+EVT:RXP2P:-112:1:XYZ") == RAK3172_RESULT_ERROR);
  assert(parse("+EVT:RXP2P:-:1:AA") == RAK3172_RESULT_ERROR);
  assert(parse("+EVT:UNRELATED") == RAK3172_RESULT_EVT);
  assert(parse("AT+NWM=0") == RAK3172_RESULT_MODE_P2P);
  assert(parse("AT+NWM=1") == RAK3172_RESULT_MODE_OTHER);
  assert(parse("RAKwireless RAK3172") == RAK3172_RESULT_BOOT);

  ground_rx_record_t record = {0};
  const char *event = "+EVT:RXP2P:-90:4:00ffAa";
  assert(rak3172_decode_rx(event, strlen(event), record.payload, 255) == 3);
  assert(record.payload[0] == 0 && record.payload[1] == 255 && record.payload[2] == 170);
  assert(rak3172_decode_rx(event, strlen(event), record.payload, 2) == 0);
  assert(rak3172_decode_rx(event, strlen(event) - 1, record.payload, 255) == 0);
  assert(rak3172_decode_rx(NULL, 0, record.payload, 255) == 0);
  event = "+EVT:RXP2P:-90:4:GG";
  assert(rak3172_decode_rx(event, strlen(event), record.payload, 255) == 0);
  char maximum[600];
  size_t prefix_len = strlen("+EVT:RXP2P:-90:4:");
  memcpy(maximum, "+EVT:RXP2P:-90:4:", prefix_len);
  memset(maximum + prefix_len, 'F', 512);
  assert(rak3172_decode_rx(maximum, prefix_len + 510, record.payload, 255) == 255);
  for (size_t i = 0; i < 255; ++i) assert(record.payload[i] == 255);
  assert(rak3172_decode_rx(maximum, prefix_len + 512, record.payload, 255) == 0);
  assert(!ground_rx_fifo_try_push(&record)); /* Real placeholder stores nothing. */

  assert(telemetry_frame_crc16((const uint8_t *)"123456789", 9) == 0x29b1u);
  gps_fix_t fix = {.timestamp_epoch = 0x01020304u, .nmea_time_utc = 0x05060708u,
      .latitude = -2, .longitude = 0x11223344, .altitude_msl = -10,
      .fix_quality = 1, .num_sats = 12, .hdop = 15, .course = 1234, .speed = 5678};
  uint8_t frame[32];
  const uint8_t expected[] = {0xaa,0x55,1,1,255,1,2,3,4,5,6,7,8,
      255,255,255,254,0x11,0x22,0x33,0x44,255,246,1,12,15,4,210,22,46};
  assert(telemetry_frame_serialize(&fix, 255, frame, sizeof(frame)) == 32);
  assert(memcmp(frame, expected, sizeof(expected)) == 0);
  /* Golden CRC obtained independently for the bytes above. */
  assert(frame[30] == 0xb9 && frame[31] == 0xd4);
  assert(telemetry_frame_serialize(&fix, 0, frame, 31) == 0);
  char hex[65];
  assert(telemetry_frame_hex_encode(frame, 32, hex, sizeof(hex)) == 64);
  assert(strncmp(hex, "AA550101FF01020304", 18) == 0);
  assert(telemetry_frame_hex_encode(frame, 32, hex, 64) == 0);
  puts("protocol/telemetry tests passed");
}
