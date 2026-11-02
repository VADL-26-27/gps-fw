#include "../examples/lora_receiver/telemetry_decode.h"
#include "telemetry_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8U * i));
}
int main() {
  uint8_t data[44];
  const char *hex = TELEMETRY_FIXTURE_HEX;
  for (unsigned i = 0; i < sizeof(data); ++i) {
    unsigned value;
    assert(sscanf(hex + 2U * i, "%2x", &value) == 1);
    data[i] = uint8_t(value);
  }
  DecodedTelemetry fix;
  assert(decodeTelemetry(data, sizeof(data), fix));
  assert(fix.i_tow_ms == 123456700U && fix.sequence == 42);
  assert(fix.latitude_e7 == 361432112 && fix.longitude_e7 == -868055040);
  assert(fix.altitude_msl_mm == 168730);
  assert(fix.velocity_north_mm_s == -1234 && fix.velocity_east_mm_s == 5678);
  assert(fix.velocity_down_mm_s == -9012);
  assert(fix.horizontal_accuracy_mm == 700 && fix.vertical_accuracy_mm == 1200);
  assert(fix.fix_type == 3 && fix.satellites_used == 25);
  assert(fix.fix_flags == 0x83 && fix.time_valid_flags == 7);
  assert(!decodeTelemetry(nullptr, 44, fix));
  assert(!decodeTelemetry(data, 43, fix));
  assert(!decodeTelemetry(data, 45, fix));
  put32(data + 4, 900000001U);
  assert(!decodeTelemetry(data, sizeof(data), fix));
  put32(data + 4, 361432112U);
  data[36] = 0;
  assert(!decodeTelemetry(data, sizeof(data), fix));
  data[36] = 3; data[38] = 0;
  assert(!decodeTelemetry(data, sizeof(data), fix));

  TelemetrySequenceTracker sequences;
  assert(sequences.observe(42).kind == SequenceKind::First);
  assert(sequences.observe(43).gap == 0);
  assert(sequences.observe(46).gap == 2 && sequences.unreceived_records == 2);
  assert(sequences.observe(46).kind == SequenceKind::Duplicate);
  assert(sequences.duplicates == 1);
  assert(sequences.observe(1).kind == SequenceKind::RestartOrReordered);
  assert(sequences.backward_changes == 1 && sequences.unreceived_records == 2);
  TelemetrySequenceTracker wrap;
  wrap.observe(UINT32_MAX);
  assert(wrap.observe(0).kind == SequenceKind::Forward);
  assert(wrap.unreceived_records == 0);
  puts("Receiver tests passed: wire layout, signed values, validation, sequences.");
}
