#ifndef GPS_H_
#define GPS_H_

#include <stdint.h>
#include <stdbool.h>

#define PACKET_SIZE 100U

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

typedef struct {
  gps_fix_t fix;
  uint32_t sequence;
} gps_transmit_t;

_Static_assert(sizeof(gps_mailbox_t) == 56, "Unexpected gps_mailbox_t size");
_Static_assert(sizeof(gps_transmit_t) == 44, "Unexpected gps_transmit_t size");

bool validate_packet(const uint8_t *packet);
void updateFix(const uint8_t *u_blox_packet, gps_mailbox_t *mailbox);
uint32_t calc_crc(const gps_mailbox_t *mailbox);
void fillPayloads(gps_mailbox_t *mailbox, gps_transmit_t *rf);
#endif // GPS_H_
