#ifndef GPS_STRUCT_H_
#define GPS_STRUCT_H_

#include <stdint.h>

#include <stdint.h>

typedef struct {
  uint32_t i_tow_ms; // GPS time of week, milliseconds

  int32_t latitude_e7;     // Degrees × 10^7
  int32_t longitude_e7;    // Degrees × 10^7
  int32_t altitude_msl_mm; // Height above mean sea level

  int32_t velocity_north_mm_s;
  int32_t velocity_east_mm_s;
  int32_t velocity_down_mm_s;

  uint32_t horizontal_accuracy_mm;
  uint32_t vertical_accuracy_mm;

  uint8_t fix_type;         // NAV-PVT fixType
  uint8_t satellites_used;  // NAV-PVT numSV
  uint8_t fix_flags;        // NAV-PVT flags, including gnssFixOK
  uint8_t time_valid_flags; // NAV-PVT valid: date/time validity
} gps_fix_t;

_Static_assert(sizeof(gps_fix_t) == 40, "Unexpected gps_fix_t size");

typedef struct {
  uint64_t unix_time_ms; // Full UTC date/time, milliseconds since epoch
  gps_fix_t fix;
  uint32_t sequence;
  uint32_t crc;
} gps_mailbox_t;

_Static_assert(sizeof(gps_mailbox_t) == 56, "Unexpected gps_mailbox_t size");

#endif // GPS_STRUCT_H_
