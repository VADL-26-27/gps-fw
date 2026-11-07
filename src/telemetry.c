#include "telemetry.h"
#include "lora.h"
#include <stddef.h>

volatile telemetry_stats_t telemetry_stats;
static uint32_t record_sequence;

bool telemetry_tick(const gps_stream_t *stream, const gps_mailbox_t *latest,
                    uint32_t now, gps_mailbox_t *record, gps_transmit_t *rf) {
  if (stream == NULL || latest == NULL || record == NULL || rf == NULL) {
    return false;
  }
  ++telemetry_stats.tick_count;
  bool have_fresh_fix = false;
  if (!stream->have_fix || !stream->fix_available) {
    ++telemetry_stats.skipped_no_fix_count;
  } else if (!gps_stream_fix_fresh(stream, now)) {
    ++telemetry_stats.skipped_stale_count;
  } else {
    have_fresh_fix = true;
  }

  if (have_fresh_fix) {
    *record = *latest;
    record->sequence = ++record_sequence;
    fillPayloads(record, rf);
    ++telemetry_stats.record_count;
    GPIOA->BSRR = (1U << 16);
  } else {
    ++telemetry_stats.no_lock_count;
    GPIOA->BSRR = (1U << 0);
  }

  if (!lora_is_ready()) {
    ++telemetry_stats.skipped_radio_unready_count;
  } else if (lora_tx_busy()) {
    ++telemetry_stats.skipped_radio_busy_count;
  } else if (!(have_fresh_fix ? transmit_packet(rf) : transmit_no_lock())) {
    ++telemetry_stats.tx_queue_failed_count;
  } else {
    ++telemetry_stats.tx_queued_count;
    if (!have_fresh_fix)
      ++telemetry_stats.no_lock_tx_queued_count;
  }
  return have_fresh_fix;
}
