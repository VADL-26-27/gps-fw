#ifndef TELEMETRY_FRAME_H
#define TELEMETRY_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_SYNC_0 0xAAu
#define TELEMETRY_SYNC_1 0x55u
#define TELEMETRY_VERSION 1u
#define TELEMETRY_MSG_GPS_FIX 0x01u

#define TELEMETRY_PAYLOAD_LEN 25u
#define TELEMETRY_FRAME_LEN 32u
#define TELEMETRY_HEX_BUF_LEN 65u

typedef struct {
  uint32_t timestamp_epoch;
  uint32_t nmea_time_utc;
  int32_t latitude;
  int32_t longitude;
  int16_t altitude_msl;
  uint8_t fix_quality;
  uint8_t num_sats;
  uint8_t hdop;
  uint16_t course;
  uint16_t speed;
} gps_fix_t;

typedef struct {
  uint8_t sync[2];
  uint8_t version;
  uint8_t msg_type;
  uint8_t seq_num;
  gps_fix_t payload;
  uint16_t checksum;
} telemetry_frame_t;

uint16_t telemetry_frame_crc16(const uint8_t *data, size_t len);
size_t telemetry_frame_serialize(const gps_fix_t *fix, uint8_t seq_num,
                                 uint8_t *out, size_t out_cap);
size_t telemetry_frame_hex_encode(const uint8_t *bin, size_t bin_len,
                                  char *hex, size_t hex_cap);

#endif