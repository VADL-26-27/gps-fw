#ifndef TELEMETRY_FRAME_H
#define TELEMETRY_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include "gps_structs.h"

#define TELEMETRY_SYNC_0 0xAAu
#define TELEMETRY_SYNC_1 0x55u
#define TELEMETRY_VERSION 1u
#define TELEMETRY_MSG_GPS_FIX 0x01u

#define TELEMETRY_PAYLOAD_LEN 25u
#define TELEMETRY_FRAME_LEN 32u
#define TELEMETRY_HEX_BUF_LEN 65u


uint16_t telemetry_frame_crc16(const uint8_t *data, size_t len);
size_t telemetry_frame_serialize(const gps_fix_t *fix, uint8_t seq_num,
                                 uint8_t *out, size_t out_cap);
size_t telemetry_frame_hex_encode(const uint8_t *bin, size_t bin_len,
                                  char *hex, size_t hex_cap);

#endif
