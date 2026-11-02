#include "gps.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void checksum(uint8_t *p) {
  uint8_t a = 0, b = 0;
  for (unsigned i = 2; i < 98; ++i) { a += p[i]; b += a; }
  p[98] = a; p[99] = b;
}

static void i32(uint8_t *p, int32_t value) {
  uint32_t v = (uint32_t)value;
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}

static void valid_frame(uint8_t *p) {
  memset(p, 0, PACKET_SIZE);
  p[0] = 0xB5; p[1] = 0x62; p[2] = 1; p[3] = 7; p[4] = 92;
  p[6+20] = 3; p[6+21] = 1;
  i32(p+6+24, -868055040);
  i32(p+6+28, 361432112);
  checksum(p);
}

static void date(uint8_t *p, unsigned year, unsigned month, unsigned day) {
  p[4] = (uint8_t)year; p[5] = (uint8_t)(year >> 8);
  p[6] = month; p[7] = day;
}

static void test_update(void) {
  uint8_t frame[PACKET_SIZE];
  valid_frame(frame);
  uint8_t *p = frame + 6;
  i32(p, 123456700);
  date(p, 2026, 10, 30);
  p[8] = 12; p[9] = 34; p[10] = 56; p[11] = 7;
  i32(p+16, 250000000);
  p[21] = 0x83; p[23] = 25;
  i32(p+32, 200000); /* Ellipsoid height must not be used as MSL. */
  i32(p+36, 168730);
  i32(p+40, 700); i32(p+44, 1200);
  i32(p+48, -1234); i32(p+52, 5678); i32(p+56, -9012);
  checksum(frame);
  assert(validate_packet(frame));
  gps_mailbox_t box = {0}; box.sequence = 41; box.crc = 0xDEADBEEF;
  updateFix(frame, &box);
  assert(box.fix.i_tow_ms == 123456700);
  assert(box.fix.latitude_e7 == 361432112);
  assert(box.fix.longitude_e7 == -868055040);
  assert(box.fix.altitude_msl_mm == 168730);
  assert(box.fix.horizontal_accuracy_mm == 700);
  assert(box.fix.vertical_accuracy_mm == 1200);
  assert(box.fix.velocity_north_mm_s == -1234);
  assert(box.fix.velocity_east_mm_s == 5678);
  assert(box.fix.velocity_down_mm_s == -9012);
  assert(box.fix.fix_type == 3 && box.fix.satellites_used == 25);
  assert(box.fix.fix_flags == 0x83 && box.fix.time_valid_flags == 7);
  assert(box.sequence == 42 && box.crc == 0);
  assert(box.unix_time_ms == 1793363696250ULL);

  i32(p+16, -1); updateFix(frame, &box);
  assert(box.unix_time_ms == 1793363695999ULL);
  i32(p+16, -250000000); updateFix(frame, &box);
  assert(box.unix_time_ms == 1793363695750ULL);
  p[8] = p[9] = p[10] = 0; i32(p+16, 0);
  date(p, 2000, 2, 29); updateFix(frame, &box);
  assert(box.unix_time_ms == 951782400000ULL);
  date(p, 2100, 3, 1); updateFix(frame, &box);
  assert(box.unix_time_ms == 4107542400000ULL);
  date(p, 2100, 2, 29); updateFix(frame, &box);
  assert(box.unix_time_ms == 0); /* 2100 is not a leap year. */
  date(p, 2026, 10, 30); p[11] = 0; updateFix(frame, &box);
  assert(box.unix_time_ms == 0 && box.fix.time_valid_flags == 0);
  p[11] = 7; p[10] = 60; updateFix(frame, &box);
  assert(box.unix_time_ms == 0); /* POSIX leap-second policy. */
  p[10] = 0; i32(p+40, -1); updateFix(frame, &box);
  assert(box.fix.horizontal_accuracy_mm == UINT32_MAX);
  gps_mailbox_t saved = box;
  updateFix(NULL, &box); updateFix(frame, NULL);
  assert(memcmp(&saved, &box, sizeof(box)) == 0);
}

static void test_crc(void) {
  /* Expected values generated independently with Python struct.pack + zlib. */
  gps_mailbox_t box = {0};
  assert(calc_crc(&box) == 0xCC2B0FB0U);
  memset(&box, 0xFF, sizeof(box));
  assert(calc_crc(&box) == 0xEB6F5BB4U);
  box = (gps_mailbox_t){
    .unix_time_ms = 1793363696250ULL,
    .fix = {
      .i_tow_ms = 123456700, .latitude_e7 = 361432112,
      .longitude_e7 = -868055040, .altitude_msl_mm = 168730,
      .velocity_north_mm_s = -1234, .velocity_east_mm_s = 5678,
      .velocity_down_mm_s = -9012, .horizontal_accuracy_mm = 700,
      .vertical_accuracy_mm = 1200, .fix_type = 3, .satellites_used = 25,
      .fix_flags = 0x83, .time_valid_flags = 7,
    },
    .sequence = 42, .crc = 0xDEADBEEF,
  };
  assert(calc_crc(&box) == 0x7B0BE0DEU);
  box.crc = 0;
  assert(calc_crc(&box) == 0x7B0BE0DEU); /* Existing CRC is excluded. */
  uint8_t *bytes = (uint8_t *)&box;
  for (unsigned i = 0; i < 52; ++i) {
    bytes[i] ^= 1;
    assert(calc_crc(&box) != 0x7B0BE0DEU);
    bytes[i] ^= 1;
  }
  gps_transmit_t rf = {0};
  fillPayloads(&box, &rf);
  assert(box.crc == 0x7B0BE0DEU);
  assert(memcmp(&box.fix, &rf.fix, sizeof(rf.fix)) == 0);
  assert(rf.sequence == box.sequence);
  assert(calc_crc(NULL) == 0);
}

int main(void) {
  uint8_t p[PACKET_SIZE];
  assert(!validate_packet(NULL));
  valid_frame(p); assert(validate_packet(p));
  for (unsigned i = 0; i < PACKET_SIZE; ++i) {
    valid_frame(p); p[i] ^= 1;
    assert(!validate_packet(p));
  }
  for (unsigned i = 0; i < 6; ++i) {
    valid_frame(p); p[i] ^= 1; checksum(p);
    assert(!validate_packet(p));
  }
  for (unsigned fix = 0; fix <= 5; ++fix) {
    valid_frame(p); p[6+20] = fix; checksum(p);
    assert(validate_packet(p) == (fix == 3));
  }
  valid_frame(p); p[6+21] = 0; checksum(p);
  assert(!validate_packet(p));
  int32_t lats[] = {-900000001, -900000000, 0, 900000000, 900000001};
  int32_t lons[] = {-1800000001, -1800000000, 0, 1800000000, 1800000001};
  for (unsigned i = 0; i < 5; ++i) {
    valid_frame(p); i32(p+6+28, lats[i]); checksum(p);
    assert(validate_packet(p) == (i > 0 && i < 4));
    valid_frame(p); i32(p+6+24, lons[i]); checksum(p);
    assert(validate_packet(p) == (i > 0 && i < 4));
  }
  valid_frame(p); p[6+11] = 0; checksum(p);
  assert(validate_packet(p)); /* Position can be valid without usable UTC. */
  test_update();
  test_crc();
  puts("GPS validation, decoding, and CRC tests passed");
}
