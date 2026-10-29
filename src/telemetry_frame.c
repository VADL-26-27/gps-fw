#include "telemetry_frame.h"
#include <math.h>
#include <limits.h>

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
/* Convert the GPS task's SI values to the existing version-1 wire units.
 * Clamp before conversion so nonfinite/out-of-range fixes cannot cause UB. */
static int32_t scaled(double value, double scale, int32_t low, int32_t high) {
  if (!isfinite(value)) return 0;
  value *= scale;
  if (value <= low) return low;
  if (value >= high) return high;
  return (int32_t)round(value);
}
size_t telemetry_frame_serialize(const gps_fix_t *fix, uint8_t seq_num,
                                 uint8_t *out, size_t out_cap) {
  if (!fix || !out || out_cap < TELEMETRY_FRAME_LEN) return 0u;
  out[0] = TELEMETRY_SYNC_0;
  out[1] = TELEMETRY_SYNC_1;
  out[2] = TELEMETRY_VERSION;
  out[3] = TELEMETRY_MSG_GPS_FIX;
  out[4] = seq_num;
  put32(out + 5, (uint32_t)(fix->timestamp_unix_ms / 1000u));
  put32(out + 9, (uint32_t)((fix->timestamp_unix_ms % 86400000u) / 10u));
  put32(out + 13, (uint32_t)scaled(fix->latitude_deg, 1e7, INT32_MIN, INT32_MAX));
  put32(out + 17, (uint32_t)scaled(fix->longitude_deg, 1e7, INT32_MIN, INT32_MAX));
  put16(out + 21, (uint16_t)scaled(fix->altitude_msl_m, 10, INT16_MIN, INT16_MAX));
  out[23] = fix->fix_quality;
  out[24] = fix->num_sats;
  out[25] = (uint8_t)scaled(fix->hdop, 10, 0, UINT8_MAX);
  put16(out + 26, (uint16_t)scaled(fix->course_deg, 10, 0, UINT16_MAX));
  put16(out + 28, (uint16_t)scaled(fix->ground_speed_mps, 100, 0, UINT16_MAX));
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
