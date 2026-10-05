#ifndef GPS_STRUCTS_H_
#define GPS_STRUCTS_H_

#include <stdint.h>

typedef struct {
  uint32_t timestamp_epoch;
  uint32_t nmea_time_utc;
  int32_t lattitude;
  int32_t longitude;
  int16_t altitude_msl;
  uint16_t course;
  uint16_t speed;
  uint8_t fix_quality;
  uint8_t num_sats;
  uint8_t hdop;
  uint8_t reserved[3]; // reserved padding to make struct 28 bytes
} gps_fix_t;

typedef struct {
  uint8_t sync1;
  uint8_t sync2;
  uint8_t msg_type;
  uint8_t seq_num;
  gps_fix_t payload; // 28 bytes
  uint16_t checksum;
} telemetry_frame_t;

_Static_assert(sizeof(gps_fix_t) == 28, "gps_fix_t layout changed");

_Static_assert(sizeof(telemetry_frame_t) == 36,
               "telemetry_frame_t layout changed");
#endif // GPS_STRUCTS_H_
