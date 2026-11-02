#include "gps.h"
#include <stddef.h>
#include <string.h>

static uint32_t read_u32_le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
         ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int32_t read_i32_le(const uint8_t *bytes) {
  uint32_t raw = read_u32_le(bytes);
  int32_t value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

bool validate_packet(const uint8_t *packet) {
  if (packet == NULL) {
    return false;
  }

  /* Sync, NAV-PVT class/ID, and 92-byte little-endian payload length. */
  if (packet[0] != 0xB5U || packet[1] != 0x62U || packet[2] != 0x01U ||
      packet[3] != 0x07U || packet[4] != 92U || packet[5] != 0U) {
    return false;
  }

  uint8_t ck_a = 0U;
  uint8_t ck_b = 0U;
  for (uint32_t i = 2U; i < PACKET_SIZE - 2U; ++i) {
    ck_a = (uint8_t)(ck_a + packet[i]);
    ck_b = (uint8_t)(ck_b + ck_a);
  }
  if (packet[PACKET_SIZE - 2U] != ck_a || packet[PACKET_SIZE - 1U] != ck_b) {
    return false;
  }

  const uint8_t *payload = packet + 6U;

  /* Require a 3D GNSS fix for position-and-altitude telemetry.
   * NAV-PVT fixType is not the same encoding as NMEA GGA quality.
   */
  if (payload[20] != 3U || (payload[21] & 0x01U) == 0U) {
    return false;
  }

  int32_t longitude = read_i32_le(payload + 24U);
  int32_t latitude = read_i32_le(payload + 28U);
  return latitude >= -900000000L && latitude <= 900000000L &&
         longitude >= -1800000000L && longitude <= 1800000000L;
}

static bool leap_year(uint32_t year) {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

static uint64_t nav_pvt_unix_ms(const uint8_t *p) {
  /* validDate, validTime and fullyResolved must all be set. */
  if ((p[11] & 0x07U) != 0x07U) {
    return 0U;
  }

  uint32_t year = (uint32_t)p[4] | ((uint32_t)p[5] << 8);
  uint32_t month = p[6], day = p[7];
  static const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};

  if (year < 1970U || year > 9999U || month < 1U || month > 12U || p[8] > 23U ||
      p[9] > 59U || p[10] > 59U) {
    return 0U;
  }
  uint32_t days_in_month = month_days[month - 1U];
  if (month == 2U && leap_year(year)) {
    ++days_in_month;
  }
  if (day == 0U || day > days_in_month) {
    return 0U;
  }

  uint32_t previous_year = year - 1U;
  uint32_t days =
      365U * (year - 1970U) +
      (previous_year / 4U - previous_year / 100U + previous_year / 400U) -
      (1969U / 4U - 1969U / 100U + 1969U / 400U);
  for (uint32_t m = 1U; m < month; ++m) {
    days += month_days[m - 1U];
    if (m == 2U && leap_year(year)) {
      ++days;
    }
  }
  days += day - 1U;

  int32_t nano = read_i32_le(p + 16U);
  if (nano < -1000000000L || nano > 1000000000L) {
    return 0U;
  }
  int64_t fraction_ms = nano / 1000000L;
  if (nano < 0 && nano % 1000000L != 0) {
    --fraction_ms; /* Floor to milliseconds, including negative fractions. */
  }
  int64_t seconds =
      (int64_t)days * 86400 + (int64_t)p[8] * 3600 + (int64_t)p[9] * 60 + p[10];
  int64_t timestamp = seconds * 1000 + fraction_ms;
  return timestamp < 0 ? 0U : (uint64_t)timestamp;
}

void updateFix(const uint8_t *u_blox_packet, gps_mailbox_t *mailbox) {
  if (u_blox_packet == NULL || mailbox == NULL) {
    return;
  }

  const uint8_t *p = u_blox_packet + 6U;

  memcpy(&mailbox->fix.i_tow_ms, p + 0U, sizeof(mailbox->fix.i_tow_ms));
  memcpy(&mailbox->fix.longitude_e7, p + 24U,
         sizeof(mailbox->fix.longitude_e7));
  memcpy(&mailbox->fix.latitude_e7, p + 28U, sizeof(mailbox->fix.latitude_e7));
  memcpy(&mailbox->fix.altitude_msl_mm, p + 36U,
         sizeof(mailbox->fix.altitude_msl_mm));
  memcpy(&mailbox->fix.horizontal_accuracy_mm, p + 40U,
         sizeof(mailbox->fix.horizontal_accuracy_mm));
  memcpy(&mailbox->fix.vertical_accuracy_mm, p + 44U,
         sizeof(mailbox->fix.vertical_accuracy_mm));
  memcpy(&mailbox->fix.velocity_north_mm_s, p + 48U,
         sizeof(mailbox->fix.velocity_north_mm_s));
  memcpy(&mailbox->fix.velocity_east_mm_s, p + 52U,
         sizeof(mailbox->fix.velocity_east_mm_s));
  memcpy(&mailbox->fix.velocity_down_mm_s, p + 56U,
         sizeof(mailbox->fix.velocity_down_mm_s));

  memcpy(&mailbox->fix.fix_type, p + 20U, sizeof(mailbox->fix.fix_type));
  memcpy(&mailbox->fix.satellites_used, p + 23U,
         sizeof(mailbox->fix.satellites_used));
  memcpy(&mailbox->fix.fix_flags, p + 21U, sizeof(mailbox->fix.fix_flags));
  memcpy(&mailbox->fix.time_valid_flags, p + 11U,
         sizeof(mailbox->fix.time_valid_flags));

  mailbox->unix_time_ms = nav_pvt_unix_ms(p);
  ++mailbox->sequence;
  mailbox->crc = 0U;
}

static void write_u32_le(uint8_t *out, uint32_t value) {
  for (uint32_t i = 0U; i < 4U; ++i) {
    out[i] = (uint8_t)(value >> (8U * i));
  }
}

static void write_u64_le(uint8_t *out, uint64_t value) {
  for (uint32_t i = 0U; i < 8U; ++i) {
    out[i] = (uint8_t)(value >> (8U * i));
  }
}

/* Canonical flash layout: timestamp [0..7], fix [8..47], sequence [48..51].
 * The stored CRC goes at [52..55] and is excluded from this calculation.
 * Flash serialization must use this same field order and little-endian format.
 */
static void serialize_log_data(const gps_mailbox_t *mailbox, uint8_t out[52]) {
  const gps_fix_t *fix = &mailbox->fix;
  write_u64_le(out + 0U, mailbox->unix_time_ms);
  write_u32_le(out + 8U, fix->i_tow_ms);
  write_u32_le(out + 12U, (uint32_t)fix->latitude_e7);
  write_u32_le(out + 16U, (uint32_t)fix->longitude_e7);
  write_u32_le(out + 20U, (uint32_t)fix->altitude_msl_mm);
  write_u32_le(out + 24U, (uint32_t)fix->velocity_north_mm_s);
  write_u32_le(out + 28U, (uint32_t)fix->velocity_east_mm_s);
  write_u32_le(out + 32U, (uint32_t)fix->velocity_down_mm_s);
  write_u32_le(out + 36U, fix->horizontal_accuracy_mm);
  write_u32_le(out + 40U, fix->vertical_accuracy_mm);
  out[44] = fix->fix_type;
  out[45] = fix->satellites_used;
  out[46] = fix->fix_flags;
  out[47] = fix->time_valid_flags;
  write_u32_le(out + 48U, mailbox->sequence);
}

uint32_t calc_crc(const gps_mailbox_t *mailbox) {
  if (mailbox == NULL) {
    return 0U;
  }

  uint8_t data[52];
  serialize_log_data(mailbox, data);

  uint32_t crc = 0xFFFFFFFFU;
  for (uint32_t i = 0U; i < sizeof(data); ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 1U) ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

void fillPayloads(gps_mailbox_t *mailbox, gps_transmit_t *rf) {
  if (mailbox == NULL || rf == NULL) {
    return;
  }

  mailbox->crc = calc_crc(mailbox);

  memcpy(&rf->fix, &mailbox->fix, sizeof(rf->fix));
  memcpy(&rf->sequence, &mailbox->sequence, sizeof(rf->sequence));
}
