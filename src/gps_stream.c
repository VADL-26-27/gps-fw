#include "gps_stream.h"
#include <stddef.h>
#include <string.h>

void gps_stream_init(gps_stream_t *stream) {
  if (stream != NULL) {
    memset(stream, 0, sizeof(*stream));
  }
}

void gps_stream_reset(gps_stream_t *stream) {
  if (stream != NULL) {
    stream->used = 0U;
    stream->skip_remaining = 0U;
    stream->fix_available = false;
  }
}

static void discard_prefix(gps_stream_t *stream, uint32_t count) {
  stream->used -= count;
  memmove(stream->packet, stream->packet + count, stream->used);
}

static bool checksum_valid(const uint8_t *packet) {
  uint8_t a = 0U, b = 0U;
  for (uint32_t i = 2U; i < PACKET_SIZE - 2U; ++i) {
    a = (uint8_t)(a + packet[i]);
    b = (uint8_t)(b + a);
  }
  return packet[98] == a && packet[99] == b;
}

bool gps_stream_feed(gps_stream_t *stream, uint8_t byte, uint32_t now,
                     gps_mailbox_t *mailbox) {
  if (stream == NULL || mailbox == NULL) {
    return false;
  }
  if ((stream->used != 0U || stream->skip_remaining != 0U) &&
      (uint32_t)(now - stream->frame_started_ms) >= GPS_FRAME_TIMEOUT_MS) {
    ++stream->framing_error_count;
    stream->used = 0U;
    stream->skip_remaining = 0U;
  }
  if (stream->skip_remaining != 0U) {
    --stream->skip_remaining;
    return false;
  }
  if (stream->used == 0U) {
    stream->frame_started_ms = now;
  }
  stream->packet[stream->used++] = byte;

  /* Re-scan retained bytes after a bad frame, preserving any prefix of the
   * next frame already consumed when the previous frame lost a byte.
   */
  while (stream->used != 0U) {
    const uint8_t *p = stream->packet;
    if (p[0] != 0xB5U) {
      discard_prefix(stream, 1U);
      continue;
    }
    if (stream->used < 2U) {
      return false;
    }
    if (p[1] != 0x62U) {
      discard_prefix(stream, 1U); /* B5 B5 62 keeps the second B5. */
      continue;
    }
    if (stream->used < 6U) {
      return false;
    }
    uint32_t payload_length = (uint32_t)p[4] | ((uint32_t)p[5] << 8);
    if (payload_length > GPS_MAX_UBX_PAYLOAD ||
        (p[2] == 1U && p[3] == 7U && payload_length != 92U)) {
      ++stream->framing_error_count;
      discard_prefix(stream, 1U);
      continue;
    }
    if (p[2] != 1U || p[3] != 7U) {
      /* Respect other UBX message lengths, including frames > our buffer.
       * Their payload must not be mistaken for a NAV-PVT header.
       */
      ++stream->skipped_message_count;
      uint32_t total = payload_length + 8U;
      if (stream->used >= total) {
        discard_prefix(stream, total);
        continue;
      }
      stream->skip_remaining = total - stream->used;
      stream->used = 0U;
      return false;
    }
    if (stream->used < PACKET_SIZE) {
      return false;
    }
    if (!checksum_valid(p)) {
      ++stream->checksum_error_count;
      discard_prefix(stream, 1U);
      continue;
    }
    stream->used = 0U;
    if (!validate_packet(p)) {
      ++stream->invalid_fix_count;
      stream->fix_available = false;
      return false;
    }
    uint32_t i_tow = (uint32_t)p[6] | ((uint32_t)p[7] << 8) |
                     ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24);
    if (stream->have_fix && stream->last_i_tow_ms == i_tow) {
      ++stream->duplicate_fix_count;
      return false; /* Replayed epochs must not keep an old fix fresh. */
    }
    updateFix(p, mailbox);
    stream->last_i_tow_ms = i_tow;
    stream->last_fix_ms = now;
    stream->have_fix = true;
    stream->fix_available = true;
    ++stream->valid_fix_count;
    return true;
  }
  return false;
}

bool gps_stream_fix_fresh(const gps_stream_t *stream, uint32_t now) {
  return stream != NULL && stream->have_fix && stream->fix_available &&
         (uint32_t)(now - stream->last_fix_ms) < GPS_FIX_MAX_AGE_MS;
}
