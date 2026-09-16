#include "telemetry_frame.h"

uint16_t telemetry_frame_crc16(const uint8_t *data, size_t len) {
  /* CRC-16/IBM-3740 (CCITT-FALSE): poly 1021, init FFFF, no reflection/xorout. */
  uint16_t crc = 0xffffu;
  if (!data && len) return 0u;
  for (size_t i = 0u; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (unsigned bit = 0u; bit < 8u; ++bit)
      crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
  }
  return crc;
}
static void put16(uint8_t *out, uint16_t n) {
  out[0] = (uint8_t)(n >> 8);
  out[1] = (uint8_t)n;
}
static void put32(uint8_t *out, uint32_t n) {
  put16(out, (uint16_t)(n >> 16));
  put16(out + 2, (uint16_t)n);
}
size_t telemetry_frame_serialize(const gps_fix_t *fix, uint8_t seq_num,
                                 uint8_t *out, size_t out_cap) {
  if (!fix || !out || out_cap < TELEMETRY_FRAME_LEN) return 0u;
  out[0] = TELEMETRY_SYNC_0;
  out[1] = TELEMETRY_SYNC_1;
  out[2] = TELEMETRY_VERSION;
  out[3] = TELEMETRY_MSG_GPS_FIX;
  out[4] = seq_num;
  put32(out + 5, fix->timestamp_epoch);
  put32(out + 9, fix->nmea_time_utc);
  put32(out + 13, (uint32_t)fix->latitude);
  put32(out + 17, (uint32_t)fix->longitude);
  put16(out + 21, (uint16_t)fix->altitude_msl);
  out[23] = fix->fix_quality;
  out[24] = fix->num_sats;
  out[25] = fix->hdop;
  put16(out + 26, fix->course);
  put16(out + 28, fix->speed);
  put16(out + 30, telemetry_frame_crc16(out + 2, 28u));
  return TELEMETRY_FRAME_LEN;
}
size_t telemetry_frame_hex_encode(const uint8_t *bin, size_t bin_len,
                                  char *hex, size_t hex_cap) {
  static const char digits[] = "0123456789ABCDEF";
  if (!bin || !hex || !hex_cap || bin_len > (hex_cap - 1u) / 2u) return 0u;
  for (size_t i = 0u; i < bin_len; ++i) {
    hex[2u * i] = digits[bin[i] >> 4];
    hex[2u * i + 1u] = digits[bin[i] & 15u];
  }
  hex[bin_len * 2u] = '\0';
  return bin_len * 2u;
}
