#ifndef TELEMETRY_H_
#define TELEMETRY_H_

#include "gps_stream.h"

typedef struct {
  uint32_t tick_count;
  uint32_t record_count;
  uint32_t skipped_no_fix_count;
  uint32_t skipped_stale_count;
  uint32_t skipped_radio_busy_count;
  uint32_t skipped_radio_unready_count;
  uint32_t tx_queue_failed_count;
  uint32_t tx_queued_count;
} telemetry_stats_t;

extern volatile telemetry_stats_t telemetry_stats;

/* Call once per consumed 1 Hz timer tick, from the main loop.
 * True means record/rf contain a new snapshot, ready for logging regardless
 * of whether RF could be queued. False leaves both outputs unchanged.
 * Record sequences count these 1 Hz snapshots, independently of GNSS epochs.
 */
bool telemetry_tick(const gps_stream_t *stream, const gps_mailbox_t *latest,
                    uint32_t now, gps_mailbox_t *record, gps_transmit_t *rf);

#endif
