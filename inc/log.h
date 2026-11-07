#ifndef GPS_LOG_H_
#define GPS_LOG_H_

#include "gps.h"

typedef struct flash flash_t;

/* Sector zero is reserved, as on the flight board. Each 256-byte page has
 * seven LE words, up to four canonical 56-byte records, then a page CRC-32.
 * Unused bytes are 0xFF. CRC covers bytes 0..251, including that padding.
 */
#define GPS_LOG_START 4096U
#define GPS_LOG_MAGIC 0x31535047U /* "GPS1" */
#define GPS_LOG_PAGE_BYTES 256U
#define GPS_LOG_RECORD_BYTES 56U
#define GPS_LOG_PAGE_RECORDS 4U
#define GPS_LOG_FLUSH_MS 5000U

typedef enum {
  LOGGER_PROBING, LOGGER_CHECKING, LOGGER_READY, LOGGER_RECORDING,
  LOGGER_FINISHING, LOGGER_COMPLETE, LOGGER_OCCUPIED, LOGGER_FULL,
  LOGGER_CLEARING, LOGGER_DUMPING, LOGGER_ERROR
} logger_status_t;

typedef struct {
  uint32_t accepted_records;
  uint32_t written_records; /* Only after flash readback verification. */
  uint32_t dropped_records;
  uint32_t written_pages;
  uint32_t next_address;
} logger_stats_t;

extern volatile logger_status_t logger_status;
extern volatile logger_stats_t logger_stats;

void logger_init(flash_t *device);
void logger_poll(void);
bool logger_start(void);
bool logger_enqueue(const gps_mailbox_t *record); /* Copies; foreground only. */
bool logger_finish(void);
bool logger_is_recording(void);
bool logger_request_clear(void);
bool logger_request_dump(void);
/* Call only during a requested dump; true when its final byte is queued. */
bool logger_dump_poll(bool (*send)(const uint8_t *, uint32_t));
const char *logger_status_reply(void);

#endif
