#ifndef GPS_STRUCTS_H_
#define GPS_STRUCTS_H_

#include <stdint.h>

typedef struct {
  double latitude_deg;  // Signed decimal degrees
  double longitude_deg; // Signed decimal degrees

  float altitude_msl_m;   // Meters above mean sea level
  float ground_speed_mps; // Meters per second
  float course_deg;       // Degrees clockwise from true north
  float hdop;             // Dimensionless dilution of precision

  uint64_t timestamp_unix_ms; // UTC Unix milliseconds, when available

  uint8_t fix_quality; // GGA fix-quality code
  uint8_t num_sats;
  uint16_t valid_fields; // Bitmask: which fields are valid
} gps_fix_t;

typedef struct {
  uint8_t sync1;
  uint8_t sync2;
  uint8_t msg_type;
  uint8_t seq_num;
  gps_fix_t payload; // 48 bytes
  uint16_t checksum;
} telemetry_frame_t;

_Static_assert(sizeof(gps_fix_t) == 48, "gps_fix_t layout changed");

_Static_assert(sizeof(telemetry_frame_t) == 64,
               "telemetry_frame_t layout changed");
#endif // GPS_STRUCTS_H_
