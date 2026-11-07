#include "gps_stream.h"
#include "telemetry.h"
#include "lora.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool radio_ready = true, radio_busy, queue_ok = true;
static uint32_t send_calls;
static uint32_t no_lock_calls;
static gps_transmit_t last_sent;
bool lora_is_ready(void) { return radio_ready; }
bool lora_tx_busy(void) { return radio_busy; }
bool transmit_packet(const gps_transmit_t *packet) {
  ++send_calls;
  last_sent = *packet;
  return queue_ok;
}
bool transmit_no_lock(void) {
  ++no_lock_calls;
  return queue_ok;
}

static void put32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8U * i));
}
static void checksum(uint8_t *p) {
  uint8_t a = 0, b = 0;
  for (unsigned i = 2; i < 98; ++i) { a += p[i]; b += a; }
  p[98] = a; p[99] = b;
}
static void frame(uint8_t *p, uint32_t tow) {
  memset(p, 0, PACKET_SIZE);
  p[0] = 0xB5; p[1] = 0x62; p[2] = 1; p[3] = 7; p[4] = 92;
  put32(p + 6, tow);
  p[26] = 3; p[27] = 1;
  put32(p + 30, (uint32_t)-868055040);
  put32(p + 34, 361432112);
  checksum(p);
}
static unsigned feed(gps_stream_t *s, gps_mailbox_t *box,
                     const uint8_t *p, unsigned length, uint32_t now) {
  unsigned updates = 0;
  for (unsigned i = 0; i < length; ++i) {
    updates += gps_stream_feed(s, p[i], now, box);
    assert(s->used < PACKET_SIZE);
  }
  return updates;
}
static void test_stream(void) {
  gps_stream_t s;
  gps_mailbox_t box;
  uint8_t p[PACKET_SIZE], next[PACKET_SIZE];
  frame(p, 1000);
  for (unsigned split = 1; split < PACKET_SIZE; ++split) {
    gps_stream_init(&s); memset(&box, 0, sizeof(box));
    assert(feed(&s, &box, p, split, 0) == 0);
    assert(feed(&s, &box, p + split, PACKET_SIZE - split, 10) == 1);
    assert(box.fix.i_tow_ms == 1000 && box.sequence == 1);
  }
  gps_stream_init(&s); memset(&box, 0, sizeof(box));
  const uint8_t noise[] = {0, 0x62, 0xB5, 0xB5, 0, 0xB5};
  feed(&s, &box, noise, sizeof(noise), 0);
  assert(feed(&s, &box, p, sizeof(p), 1) == 1);

  /* Other variable-length UBX messages are skipped, including a payload
   * containing an entire valid-looking NAV-PVT frame.
   */
  uint8_t other[308] = {0xB5, 0x62, 2, 0x15, 0x2C, 1};
  frame(next, 1100);
  memcpy(other + 50, next, sizeof(next));
  assert(feed(&s, &box, other, sizeof(other), 2) == 0);
  assert(s.skipped_message_count == 1);
  assert(feed(&s, &box, next, sizeof(next), 3) == 1);

  /* Dropped or inserted payload/checksum bytes must not lose the next fix. */
  for (unsigned cut = 6; cut < PACKET_SIZE; ++cut) {
    gps_stream_init(&s); memset(&box, 0, sizeof(box));
    feed(&s, &box, p, cut, 0);
    feed(&s, &box, p + cut + 1, PACKET_SIZE - cut - 1, 0);
    assert(feed(&s, &box, next, sizeof(next), 1) == 1);
    assert(box.fix.i_tow_ms == 1100);
  }
  for (unsigned insert = 6; insert < PACKET_SIZE; ++insert) {
    gps_stream_init(&s); memset(&box, 0, sizeof(box));
    feed(&s, &box, p, insert, 0);
    gps_stream_feed(&s, 0xA3, 0, &box);
    feed(&s, &box, p + insert, PACKET_SIZE - insert, 0);
    assert(feed(&s, &box, next, sizeof(next), 1) == 1);
  }

  gps_stream_init(&s); memset(&box, 0, sizeof(box));
  p[50] ^= 1;
  assert(feed(&s, &box, p, sizeof(p), 0) == 0);
  assert(s.checksum_error_count == 1);
  p[50] ^= 1;
  const uint8_t huge[] = {0xB5, 0x62, 2, 3, 0xFF, 0xFF};
  feed(&s, &box, huge, sizeof(huge), 1);
  assert(s.framing_error_count == 1);
  assert(feed(&s, &box, p, sizeof(p), 2) == 1);

  gps_stream_init(&s); memset(&box, 0, sizeof(box));
  feed(&s, &box, p, 50, 0);
  assert(feed(&s, &box, next, sizeof(next), GPS_FRAME_TIMEOUT_MS) == 1);
  assert(s.framing_error_count == 1);

  /* A malformed length in a different UBX header cannot stall forever. */
  gps_stream_init(&s);
  const uint8_t long_header[] = {0xB5, 0x62, 2, 3, 0xFF, 0x0F};
  feed(&s, &box, long_header, sizeof(long_header), 0);
  assert(feed(&s, &box, p, sizeof(p), GPS_FRAME_TIMEOUT_MS) == 1);

  /* Bad-fix messages invalidate immediately; duplicates never refresh age. */
  assert(gps_stream_fix_fresh(&s, GPS_FRAME_TIMEOUT_MS));
  uint32_t saved_time = s.last_fix_ms, saved_sequence = box.sequence;
  assert(feed(&s, &box, p, sizeof(p), saved_time + 1000) == 0);
  assert(s.last_fix_ms == saved_time && box.sequence == saved_sequence);
  assert(!gps_stream_fix_fresh(&s, saved_time + GPS_FIX_MAX_AGE_MS));
  frame(next, 1200); next[26] = 0; checksum(next);
  feed(&s, &box, next, sizeof(next), saved_time + 1001);
  assert(!s.fix_available && s.invalid_fix_count == 1);
  frame(next, 1300);
  assert(feed(&s, &box, next, sizeof(next), saved_time + 1002) == 1);
  gps_stream_reset(&s);
  assert(!gps_stream_fix_fresh(&s, saved_time + 1003));

  gps_stream_init(&s);
  uint32_t near_wrap = UINT32_MAX - 100U;
  assert(feed(&s, &box, p, sizeof(p), near_wrap) == 1);
  assert(gps_stream_fix_fresh(&s, near_wrap + GPS_FIX_MAX_AGE_MS - 1U));
  assert(!gps_stream_fix_fresh(&s, near_wrap + GPS_FIX_MAX_AGE_MS));
  assert(!gps_stream_feed(NULL, 0, 0, &box));
  assert(!gps_stream_feed(&s, 0, 0, NULL));
}

static void test_tick(void) {
  gps_stream_t s;
  gps_stream_init(&s);
  gps_mailbox_t latest = {0}, record = {0};
  gps_transmit_t rf = {0};
  assert(!telemetry_tick(&s, &latest, 0, &record, &rf));
  assert(telemetry_stats.skipped_no_fix_count == 1 && send_calls == 0);
  assert(no_lock_calls == 1 && telemetry_stats.no_lock_tx_queued_count == 1);
  assert(telemetry_stats.record_count == 0); /* No flash record without lock. */
  uint8_t p[PACKET_SIZE];
  /* Ten GNSS epochs create only one record when the 1 Hz tick is handled. */
  for (unsigned i = 0; i < 10; ++i) {
    frame(p, i * 100U);
    assert(feed(&s, &latest, p, sizeof(p), i * 100U) == 1);
  }
  assert(latest.sequence == 10 && telemetry_stats.record_count == 0);
  assert(telemetry_tick(&s, &latest, 1000, &record, &rf));
  assert(record.sequence == 1 && latest.sequence == 10);
  assert(record.crc == calc_crc(&record));
  assert(rf.sequence == 1 && last_sent.sequence == 1);
  assert(rf.fix.i_tow_ms == 900 && send_calls == 1);

  radio_busy = true;
  assert(telemetry_tick(&s, &latest, 1100, &record, &rf));
  assert(record.sequence == 2 && record.crc == calc_crc(&record));
  assert(telemetry_stats.skipped_radio_busy_count == 1 && send_calls == 1);
  radio_busy = false; radio_ready = false;
  assert(telemetry_tick(&s, &latest, 1200, &record, &rf));
  assert(record.sequence == 3 && telemetry_stats.skipped_radio_unready_count == 1);
  radio_ready = true; queue_ok = false;
  assert(telemetry_tick(&s, &latest, 1300, &record, &rf));
  assert(record.sequence == 4 && telemetry_stats.tx_queue_failed_count == 1);
  queue_ok = true;

  gps_mailbox_t saved = record;
  gps_transmit_t saved_rf = rf;
  assert(!telemetry_tick(&s, &latest, 900 + GPS_FIX_MAX_AGE_MS, &record, &rf));
  assert(telemetry_stats.skipped_stale_count == 1);
  assert(memcmp(&record, &saved, sizeof(record)) == 0);
  assert(memcmp(&rf, &saved_rf, sizeof(rf)) == 0);
  frame(p, 1000); p[26] = 0; checksum(p);
  feed(&s, &latest, p, sizeof(p), 1400);
  assert(!telemetry_tick(&s, &latest, 1401, &record, &rf));
  assert(telemetry_stats.skipped_no_fix_count == 2);
  assert(telemetry_stats.record_count == 4 && telemetry_stats.tx_queued_count == 4);
  assert(no_lock_calls == 3 && telemetry_stats.no_lock_count == 3);
  assert(telemetry_stats.no_lock_tx_queued_count == 3);
  assert(memcmp(&record, &saved, sizeof(record)) == 0);
  assert(memcmp(&rf, &saved_rf, sizeof(rf)) == 0);

  /* Status uses the same radio guards; busy/unready ticks never queue it. */
  radio_busy = true;
  assert(!telemetry_tick(&s, &latest, 1500, &record, &rf));
  assert(no_lock_calls == 3 && telemetry_stats.skipped_radio_busy_count == 2);
  radio_busy = false; radio_ready = false;
  assert(!telemetry_tick(&s, &latest, 1600, &record, &rf));
  assert(no_lock_calls == 3 && telemetry_stats.skipped_radio_unready_count == 2);
  radio_ready = true; queue_ok = false;
  assert(!telemetry_tick(&s, &latest, 1700, &record, &rf));
  assert(no_lock_calls == 4 && telemetry_stats.tx_queue_failed_count == 2);
  queue_ok = true;
  assert(!telemetry_tick(&s, &latest, 1800, &record, &rf));
  assert(no_lock_calls == 5 && telemetry_stats.no_lock_tx_queued_count == 4);
  assert(telemetry_stats.record_count == 4);
  assert(memcmp(&record, &saved, sizeof(record)) == 0);
  assert(memcmp(&rf, &saved_rf, sizeof(rf)) == 0);

  /* Reacquiring lock switches back to GPS without counting status as records. */
  frame(p, 1100);
  assert(feed(&s, &latest, p, sizeof(p), 1900) == 1);
  assert(telemetry_tick(&s, &latest, 1901, &record, &rf));
  assert(record.sequence == 5 && record.crc == calc_crc(&record));
  assert(last_sent.fix.i_tow_ms == 1100 && last_sent.sequence == 5);
  assert(telemetry_stats.record_count == 5 && no_lock_calls == 5);
}

int main(void) {
  test_stream();
  test_tick();
  puts("Pipeline tests passed: framing, byte loss/insertion, other UBX messages, "
       "freshness, fix loss, 1 Hz snapshots, sequences, CRC, radio skips, "
       "no-lock status without log records, lock recovery.");
}
