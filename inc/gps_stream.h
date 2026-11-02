#ifndef GPS_STREAM_H_
#define GPS_STREAM_H_

#include "gps.h"

#define GPS_FIX_MAX_AGE_MS 1500U
#define GPS_FRAME_TIMEOUT_MS 500U
#define GPS_MAX_UBX_PAYLOAD 4096U

/* Foreground-only parser. Partial messages survive calls and UART chunks. */
typedef struct {
  uint8_t packet[PACKET_SIZE];
  uint32_t used;
  uint32_t skip_remaining;
  uint32_t frame_started_ms;
  uint32_t last_fix_ms;
  uint32_t last_i_tow_ms;
  bool have_fix;
  bool fix_available; /* Latest reported fix usable; age is checked separately. */
  uint32_t valid_fix_count;
  uint32_t invalid_fix_count;
  uint32_t duplicate_fix_count;
  uint32_t checksum_error_count;
  uint32_t framing_error_count;
  uint32_t skipped_message_count;
} gps_stream_t;

void gps_stream_init(gps_stream_t *stream);
/* Drop partial framing and invalidate availability after UART errors/overflow. */
void gps_stream_reset(gps_stream_t *stream);
/* True only when a new valid NAV-PVT epoch updates the mailbox. */
bool gps_stream_feed(gps_stream_t *stream, uint8_t byte, uint32_t now,
                     gps_mailbox_t *mailbox);
bool gps_stream_fix_fresh(const gps_stream_t *stream, uint32_t now);

#endif
