#include "telemetry_frame.h"

uint16_t telemetry_frame_crc16(const uint8_t *data, size_t len) {
  (void)data;
  (void)len;
  return 0u;
}

size_t telemetry_frame_serialize(const gps_fix_t *fix, uint8_t seq_num,
                                 uint8_t *out, size_t out_cap) {
  (void)fix;
  (void)seq_num;
  (void)out;
  (void)out_cap;
  return 0u;
}

size_t telemetry_frame_hex_encode(const uint8_t *bin, size_t bin_len,
                                  char *hex, size_t hex_cap) {
  (void)bin;
  (void)bin_len;
  (void)hex;
  (void)hex_cap;
  return 0u;
}