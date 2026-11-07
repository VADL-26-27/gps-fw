#ifndef TELEMETRY_DECODE_H
#define TELEMETRY_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Wire offsets are explicit; no compiler packing or native endianness assumed. */
inline bool isNoLockMessage(const uint8_t *data, size_t length) {
  static const char message[] = "NO LOCK FOUND";
  return data != nullptr && length == sizeof(message) - 1U &&
         memcmp(data, message, sizeof(message) - 1U) == 0;
}

struct DecodedTelemetry {
  uint32_t i_tow_ms;
  int32_t latitude_e7, longitude_e7, altitude_msl_mm;
  int32_t velocity_north_mm_s, velocity_east_mm_s, velocity_down_mm_s;
  uint32_t horizontal_accuracy_mm, vertical_accuracy_mm;
  uint8_t fix_type, satellites_used, fix_flags, time_valid_flags;
  uint32_t sequence;
};

inline uint32_t telemetryU32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline int32_t telemetryI32(const uint8_t *p) {
  uint32_t raw = telemetryU32(p);
  int32_t result;
  memcpy(&result, &raw, sizeof(result));
  return result;
}

inline bool decodeTelemetry(const uint8_t *data, size_t length,
                            DecodedTelemetry &out) {
  if (data == nullptr || length != 44U) return false;
  DecodedTelemetry next = {};
  next.i_tow_ms = telemetryU32(data + 0U);
  next.latitude_e7 = telemetryI32(data + 4U);
  next.longitude_e7 = telemetryI32(data + 8U);
  next.altitude_msl_mm = telemetryI32(data + 12U);
  next.velocity_north_mm_s = telemetryI32(data + 16U);
  next.velocity_east_mm_s = telemetryI32(data + 20U);
  next.velocity_down_mm_s = telemetryI32(data + 24U);
  next.horizontal_accuracy_mm = telemetryU32(data + 28U);
  next.vertical_accuracy_mm = telemetryU32(data + 32U);
  next.fix_type = data[36];
  next.satellites_used = data[37];
  next.fix_flags = data[38];
  next.time_valid_flags = data[39];
  next.sequence = telemetryU32(data + 40U);
  if (next.fix_type != 3U || (next.fix_flags & 1U) == 0U ||
      next.i_tow_ms >= 604800000U ||
      next.latitude_e7 < -900000000L || next.latitude_e7 > 900000000L ||
      next.longitude_e7 < -1800000000L || next.longitude_e7 > 1800000000L) {
    return false;
  }
  out = next;
  return true;
}

enum class SequenceKind { First, Forward, Duplicate, RestartOrReordered };
struct SequenceObservation {
  SequenceKind kind;
  uint32_t gap;
};

struct TelemetrySequenceTracker {
  bool have_sequence = false;
  uint32_t last_sequence = 0U;
  uint32_t unreceived_records = 0U;
  uint32_t duplicates = 0U;
  uint32_t backward_changes = 0U;

  SequenceObservation observe(uint32_t sequence) {
    if (!have_sequence) {
      have_sequence = true;
      last_sequence = sequence;
      return {SequenceKind::First, 0U};
    }
    uint32_t delta = sequence - last_sequence;
    if (delta == 0U) {
      ++duplicates;
      return {SequenceKind::Duplicate, 0U};
    }
    last_sequence = sequence;
    if (delta >= 0x80000000U) {
      ++backward_changes;
      return {SequenceKind::RestartOrReordered, 0U};
    }
    unreceived_records += delta - 1U;
    return {SequenceKind::Forward, delta - 1U};
  }
};

#endif
