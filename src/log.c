#include "log.h"
#include "flash_mem.h"
#include "timer.h"
#include <string.h>

enum { BLOCK_HEADER = 1, BLOCK_DATA = 2, BLOCK_FOOTER = 3 };
typedef enum {
  OP_NONE, OP_PROBE, OP_BOOT_READ, OP_SECTOR_READ, OP_WRITE,
  OP_CLEAR_READ, OP_CLEAR_ERASE, OP_DUMP_READ
} operation_t;
typedef enum { DUMP_BANNER, DUMP_READ, DUMP_TAG, DUMP_DATA,
               DUMP_END, DUMP_ERROR } dump_stage_t;

_Static_assert(28U + GPS_LOG_PAGE_RECORDS * GPS_LOG_RECORD_BYTES + 4U ==
               GPS_LOG_PAGE_BYTES, "Log page layout");
_Static_assert(GPS_LOG_PAGE_BYTES == FLASH_PAGE_SIZE, "Flash page size");
_Static_assert(GPS_LOG_START == FLASH_SECTOR_SIZE, "Reserved sector size");

volatile logger_status_t logger_status;
volatile logger_stats_t logger_stats;
static flash_t *storage;
static operation_t operation;
static gps_mailbox_t queue[2U * GPS_LOG_PAGE_RECORDS];
static uint32_t head, count, queued_at;
/* Never modify page while flash_program owns its pointer. The other half of
 * the eight-record queue continues collecting samples during that write. */
static uint8_t page[GPS_LOG_PAGE_BYTES];
static uint8_t sector_data[FLASH_SECTOR_SIZE];
static uint32_t address, checked_sector, block_sequence;
static uint32_t pending_kind, pending_records, clear_address;
static bool recording, finishing, header_written;
static dump_stage_t dump_stage;
static uint32_t dump_address, dump_offset;
static logger_status_t before_dump;

static uint32_t crc32(const uint8_t *data, uint32_t length) {
  uint32_t crc = UINT32_MAX;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint32_t bit = 0; bit < 8U; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}

static void put32(uint8_t *p, uint32_t value) {
  for (uint32_t i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static void put_record(uint8_t *p, const gps_mailbox_t *record) {
  const gps_fix_t *f = &record->fix;
  put32(p, (uint32_t)record->unix_time_ms);
  put32(p + 4, (uint32_t)(record->unix_time_ms >> 32));
  put32(p + 8, f->i_tow_ms);
  put32(p + 12, (uint32_t)f->latitude_e7);
  put32(p + 16, (uint32_t)f->longitude_e7);
  put32(p + 20, (uint32_t)f->altitude_msl_mm);
  put32(p + 24, (uint32_t)f->velocity_north_mm_s);
  put32(p + 28, (uint32_t)f->velocity_east_mm_s);
  put32(p + 32, (uint32_t)f->velocity_down_mm_s);
  put32(p + 36, f->horizontal_accuracy_mm);
  put32(p + 40, f->vertical_accuracy_mm);
  p[44] = f->fix_type; p[45] = f->satellites_used;
  p[46] = f->fix_flags; p[47] = f->time_valid_flags;
  put32(p + 48, record->sequence);
  put32(p + 52, calc_crc(record));
}

static bool blank(void) {
  for (uint32_t i = 0; i < sizeof(sector_data); ++i)
    if (sector_data[i] != 0xFFU) return false;
  return true;
}

static void fail(logger_status_t status) {
  recording = finishing = false;
  logger_status = status;
}

static bool idle(void) {
  return storage != NULL && storage->initialized && !storage->busy &&
         storage->result == FLASH_RESULT_NONE && operation == OP_NONE;
}

static bool can_manage(void) {
  return idle() && !recording && !finishing &&
         (logger_status == LOGGER_READY || logger_status == LOGGER_COMPLETE ||
          logger_status == LOGGER_OCCUPIED || logger_status == LOGGER_FULL ||
          logger_status == LOGGER_ERROR);
}

static void reset_session(void) {
  head = count = block_sequence = 0;
  address = GPS_LOG_START;
  checked_sector = UINT32_MAX;
  recording = finishing = header_written = false;
  logger_stats = (logger_stats_t){.next_address = GPS_LOG_START};
}

void logger_init(flash_t *device) {
  storage = device;
  reset_session();
  operation = OP_NONE;
  logger_status = LOGGER_ERROR;
  if (flash_probe(storage)) {
    operation = OP_PROBE;
    logger_status = LOGGER_PROBING;
  }
}

bool logger_is_recording(void) { return recording; }

bool logger_start(void) {
  if (recording) return true;
  if (logger_status != LOGGER_READY || !idle()) return false;
  recording = true;
  logger_status = LOGGER_RECORDING;
  return true;
}

bool logger_enqueue(const gps_mailbox_t *record) {
  if (!recording || record == NULL) return false;
  if (count == sizeof(queue) / sizeof(queue[0])) {
    ++logger_stats.dropped_records;
    return false;
  }
  if (count == 0U) queued_at = now_ms();
  queue[(head + count) % (sizeof(queue) / sizeof(queue[0]))] = *record;
  ++count;
  ++logger_stats.accepted_records;
  return true;
}

bool logger_finish(void) {
  if (finishing || logger_status == LOGGER_COMPLETE) return true;
  if (!recording) return false;
  recording = false;
  finishing = true;
  logger_status = LOGGER_FINISHING;
  return true;
}

bool logger_request_clear(void) {
  if (!can_manage()) return false;
  /* Visit ALL sectors backwards, including beyond a blank gap. A reset during
   * clearing leaves the log prefix until last. Never erase sector zero. */
  clear_address = FLASH_CAPACITY_BYTES - FLASH_SECTOR_SIZE;
  logger_status = LOGGER_CLEARING;
  return true;
}

bool logger_request_dump(void) {
  if (!can_manage()) return false;
  before_dump = logger_status;
  logger_status = LOGGER_DUMPING;
  dump_address = GPS_LOG_START;
  dump_stage = DUMP_BANNER;
  return true;
}

static void clear_advance(void) {
  if (clear_address == GPS_LOG_START) {
    reset_session();
    logger_status = LOGGER_READY;
  } else clear_address -= FLASH_SECTOR_SIZE;
}

static void write_page(uint32_t kind, uint32_t records) {
  memset(page, 0xFF, sizeof(page));
  put32(page, GPS_LOG_MAGIC);
  put32(page + 4, kind);
  put32(page + 8, 1U); /* Format version. */
  put32(page + 12, block_sequence);
  put32(page + 16, records);
  put32(page + 20, logger_stats.written_records);
  put32(page + 24, logger_stats.dropped_records);
  for (uint32_t i = 0; i < records; ++i)
    put_record(page + 28U + GPS_LOG_RECORD_BYTES * i,
               &queue[(head + i) % (sizeof(queue) / sizeof(queue[0]))]);
  put32(page + 252, crc32(page, 252));
  if (!flash_program(storage, address, page, sizeof(page))) {
    fail(LOGGER_ERROR);
    return;
  }
  pending_kind = kind;
  pending_records = records;
  operation = OP_WRITE;
}

void logger_poll(void) {
  if (storage == NULL || storage->busy) return;
  if (operation != OP_NONE) {
    if (storage->result == FLASH_RESULT_NONE ||
        storage->result == FLASH_RESULT_PENDING) return;
    operation_t completed = operation;
    bool ok = storage->result == FLASH_RESULT_OK;
    if (!flash_ack_result(storage)) return;
    operation = OP_NONE;
    if (!ok) {
      if (completed == OP_DUMP_READ) dump_stage = DUMP_ERROR;
      else fail(LOGGER_ERROR);
      return;
    }
    switch (completed) {
    case OP_PROBE:
      logger_status = LOGGER_CHECKING;
      break;
    case OP_BOOT_READ:
      if (blank()) {
        checked_sector = GPS_LOG_START;
        logger_status = LOGGER_READY;
      } else fail(LOGGER_OCCUPIED);
      break;
    case OP_SECTOR_READ:
      if (blank()) checked_sector = address;
      else fail(LOGGER_OCCUPIED);
      break;
    case OP_WRITE:
      ++block_sequence;
      ++logger_stats.written_pages;
      logger_stats.written_records += pending_records;
      head = (head + pending_records) % (sizeof(queue) / sizeof(queue[0]));
      count -= pending_records;
      queued_at = now_ms();
      address += GPS_LOG_PAGE_BYTES;
      logger_stats.next_address = address;
      if (pending_kind == BLOCK_HEADER) header_written = true;
      if (pending_kind == BLOCK_FOOTER) {
        finishing = false;
        logger_status = LOGGER_COMPLETE;
      } else if (address == FLASH_CAPACITY_BYTES) fail(LOGGER_FULL);
      break;
    case OP_CLEAR_READ:
      if (blank()) clear_advance();
      else if (flash_erase_sector(storage, clear_address))
        operation = OP_CLEAR_ERASE;
      else fail(LOGGER_ERROR);
      break;
    case OP_CLEAR_ERASE:
      clear_advance();
      break;
    case OP_DUMP_READ:
      dump_stage = blank() ? DUMP_END : DUMP_TAG;
      break;
    default: fail(LOGGER_ERROR); break;
    }
  }
  if (operation != OP_NONE || !idle()) return;
  if (logger_status == LOGGER_CHECKING) {
    if (flash_read(storage, GPS_LOG_START, sector_data, sizeof(sector_data)))
      operation = OP_BOOT_READ;
    else fail(LOGGER_ERROR);
  } else if (logger_status == LOGGER_CLEARING) {
    if (flash_read(storage, clear_address, sector_data, sizeof(sector_data)))
      operation = OP_CLEAR_READ;
    else fail(LOGGER_ERROR);
  } else if (recording || finishing) {
    uint32_t kind = 0U;
    if (!header_written) kind = BLOCK_HEADER;
    else if (count >= GPS_LOG_PAGE_RECORDS ||
             (count != 0U && (finishing ||
              (uint32_t)(now_ms() - queued_at) >= GPS_LOG_FLUSH_MS)))
      kind = BLOCK_DATA;
    else if (finishing && count == 0U) kind = BLOCK_FOOTER;
    if (kind == 0U) return;
    if (address >= FLASH_CAPACITY_BYTES) { fail(LOGGER_FULL); return; }
    uint32_t sector = address - address % FLASH_SECTOR_SIZE;
    if (checked_sector != sector) {
      if (flash_read(storage, sector, sector_data, sizeof(sector_data)))
        operation = OP_SECTOR_READ;
      else fail(LOGGER_ERROR);
      return;
    }
    uint32_t records = kind == BLOCK_DATA ? count : 0U;
    if (records > GPS_LOG_PAGE_RECORDS) records = GPS_LOG_PAGE_RECORDS;
    write_page(kind, records);
  }
}

bool logger_dump_poll(bool (*send)(const uint8_t *, uint32_t)) {
  static const uint8_t banner[] = "\r\nGPS LOG DUMP 1\r\n";
  if (send == NULL || logger_status != LOGGER_DUMPING) return false;
  switch (dump_stage) {
  case DUMP_BANNER:
    if (send(banner, sizeof(banner) - 1U)) dump_stage = DUMP_READ;
    break;
  case DUMP_READ:
    if (operation != OP_NONE) break;
    if (dump_address == FLASH_CAPACITY_BYTES) dump_stage = DUMP_END;
    else if (flash_read(storage, dump_address, sector_data, sizeof(sector_data)))
      operation = OP_DUMP_READ;
    else dump_stage = DUMP_ERROR;
    break;
  case DUMP_TAG:
    if (send((const uint8_t *)"D", 1U)) { dump_offset = 0; dump_stage = DUMP_DATA; }
    break;
  case DUMP_DATA: {
    uint32_t n = sizeof(sector_data) - dump_offset;
    if (n > 64U) n = 64U;
    if (send(sector_data + dump_offset, n)) {
      dump_offset += n;
      if (dump_offset == sizeof(sector_data)) {
        dump_address += FLASH_SECTOR_SIZE;
        dump_stage = DUMP_READ;
      }
    }
    break;
  }
  case DUMP_END:
  case DUMP_ERROR:
    if (send((const uint8_t *)(dump_stage == DUMP_END ? "E" : "X"), 1U)) {
      logger_status = dump_stage == DUMP_END ? before_dump : LOGGER_ERROR;
      return true;
    }
    break;
  }
  return false;
}

const char *logger_status_reply(void) {
  switch (logger_status) {
  case LOGGER_PROBING: case LOGGER_CHECKING: return "LOG CHECKING\r\n";
  case LOGGER_READY: return "LOG READY\r\n";
  case LOGGER_RECORDING: return "LOG RECORDING\r\n";
  case LOGGER_FINISHING: return "LOG FINISHING\r\n";
  case LOGGER_COMPLETE: return "LOG COMPLETE\r\n";
  case LOGGER_OCCUPIED: return "LOG OCCUPIED\r\n";
  case LOGGER_FULL: return "LOG FULL\r\n";
  case LOGGER_CLEARING: return "LOG CLEARING\r\n";
  case LOGGER_DUMPING: return "LOG DUMPING\r\n";
  default: return "LOG ERROR\r\n";
  }
}
