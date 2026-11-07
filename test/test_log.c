#include "log.h"
#include "flash_mem.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[FLASH_CAPACITY_BYTES];
static flash_t flash;
static uint32_t clock_ms, writes, erases, reads, io_address, io_length;
static const uint8_t *write_pointer;
static uint8_t *read_pointer;
static enum { NONE, PROBE, READ, WRITE, ERASE } io;
static bool fail_next;
static uint8_t output[4U * FLASH_SECTOR_SIZE];
static uint32_t output_length;
static bool allow_send = true;

uint32_t now_ms(void) { return clock_ms; }
static bool begin(int kind, uint32_t address, uint32_t length) {
  assert(!flash.busy && flash.result == FLASH_RESULT_NONE && io == NONE);
  assert(address <= FLASH_CAPACITY_BYTES && length <= FLASH_CAPACITY_BYTES - address);
  io = kind; io_address = address; io_length = length;
  flash.busy = true; flash.result = FLASH_RESULT_PENDING;
  return true;
}
bool flash_probe(flash_t *f) {
  assert(f == &flash);
  f->initialized = false;
  return begin(PROBE, 0, 0);
}
bool flash_read(flash_t *f, uint32_t address, uint8_t *data, uint32_t length) {
  assert(f == &flash && f->initialized);
  read_pointer = data; ++reads;
  return begin(READ, address, length);
}
bool flash_program(flash_t *f, uint32_t address, const uint8_t *data, uint32_t length) {
  assert(f == &flash && f->initialized && address >= GPS_LOG_START);
  assert(length == 256U && address % 256U == 0);
  write_pointer = data; ++writes;
  return begin(WRITE, address, length);
}
bool flash_erase_sector(flash_t *f, uint32_t address) {
  assert(f == &flash && f->initialized && address >= GPS_LOG_START);
  assert(address % FLASH_SECTOR_SIZE == 0U);
  ++erases;
  return begin(ERASE, address, FLASH_SECTOR_SIZE);
}
bool flash_ack_result(flash_t *f) {
  assert(f == &flash && !f->busy);
  assert(f->result == FLASH_RESULT_OK || f->result == FLASH_RESULT_FAILED);
  f->result = FLASH_RESULT_NONE;
  return true;
}
static void complete_io(void) {
  if (!flash.busy) return;
  switch (io) {
  case PROBE: flash.initialized = !fail_next; break;
  case READ: memcpy(read_pointer, memory + io_address, io_length); break;
  case WRITE:
    for (uint32_t i = 0; i < (fail_next ? 20U : io_length); ++i) {
      assert(memory[io_address + i] == 0xFFU); /* No overwrite, even after reboot. */
      memory[io_address + i] &= write_pointer[i];
    }
    break;
  case ERASE: if (!fail_next) memset(memory + io_address, 0xFF, io_length); break;
  default: assert(0);
  }
  io = NONE; flash.busy = false;
  flash.result = fail_next ? FLASH_RESULT_FAILED : FLASH_RESULT_OK;
  fail_next = false;
}
static void step(void) { complete_io(); logger_poll(); ++clock_ms; }
static void pump(unsigned n) { while (n--) step(); }
static void boot(bool erase_memory) {
  if (erase_memory) memset(memory, 0xFF, sizeof(memory));
  /* MCU reset drops RAM operations, but preserves all flash bytes. */
  flash = (flash_t){0}; io = NONE; fail_next = false;
  logger_init(&flash);
  assert(!logger_is_recording());
  pump(4);
}
static uint32_t word(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static gps_mailbox_t record(uint32_t sequence) {
  gps_mailbox_t r = {.unix_time_ms = UINT64_C(1793500000123), .sequence = sequence};
  r.fix.i_tow_ms = 526236000U;
  r.fix.latitude_e7 = 361434131;
  r.fix.longitude_e7 = -868053556;
  r.fix.altitude_msl_mm = 185713;
  r.fix.velocity_north_mm_s = 148;
  r.fix.velocity_east_mm_s = -69;
  r.fix.velocity_down_mm_s = -170;
  r.fix.horizontal_accuracy_mm = 955;
  r.fix.vertical_accuracy_mm = 1049;
  r.fix.fix_type = 3; r.fix.satellites_used = 13;
  r.fix.fix_flags = 13; r.fix.time_valid_flags = 7;
  r.crc = calc_crc(&r);
  return r;
}
static bool send_bytes(const uint8_t *p, uint32_t n) {
  if (!allow_send) return false;
  assert(output_length + n <= sizeof(output));
  memcpy(output + output_length, p, n); output_length += n;
  return true;
}
static void dump(void) {
  output_length = 0;
  assert(logger_request_dump());
  allow_send = false;
  for (int i = 0; i < 5; ++i) {
    step(); assert(!logger_dump_poll(send_bytes));
  }
  assert(output_length == 0);
  allow_send = true;
  for (int i = 0; i < 10000; ++i) {
    step();
    if (logger_dump_poll(send_bytes)) return;
  }
  assert(0);
}

int main(void) {
  boot(true);
  assert(logger_status == LOGGER_READY && writes == 0 && erases == 0);
  gps_mailbox_t r = record(1);
  assert(!logger_enqueue(&r));
  assert(logger_start() && logger_start());
  logger_poll(); /* Header write in flight while both record buffers fill. */
  assert(flash.busy);
  for (unsigned i = 1; i <= 8; ++i) { r = record(i); assert(logger_enqueue(&r)); }
  r = record(9); assert(!logger_enqueue(&r));
  memset(&r, 0, sizeof(r)); /* Enqueue must own a copy. */
  assert(!logger_request_dump() && !logger_request_clear());
  pump(20);
  assert(logger_stats.written_records == 8 && logger_stats.dropped_records == 1);
  const uint8_t *data = memory + GPS_LOG_START + 256;
  assert(word(data) == GPS_LOG_MAGIC && word(data + 4) == 2);
  for (unsigned i = 0; i < 4; ++i) {
    r = record(i + 1);
    const uint8_t *p = data + 28 + i * 56;
    assert(word(p + 48) == i + 1 && word(p + 52) == r.crc);
    assert(word(p + 12) == (uint32_t)r.fix.latitude_e7);
    assert(word(p + 16) == (uint32_t)r.fix.longitude_e7);
  }
  r = record(9); assert(logger_enqueue(&r));
  pump(3); assert(logger_stats.written_records == 8);
  assert(logger_finish() && !logger_is_recording() && !logger_enqueue(&r));
  assert(!logger_request_dump() && !logger_request_clear());
  pump(15);
  assert(logger_status == LOGGER_COMPLETE && logger_stats.written_records == 9);
  assert(!logger_start() && logger_finish());
  dump();
  static const char banner[] = "\r\nGPS LOG DUMP 1\r\n";
  assert(memcmp(output, banner, sizeof(banner) - 1) == 0);
  assert(output_length == sizeof(banner) - 1 + 1 + FLASH_SECTOR_SIZE + 1);
  assert(output[sizeof(banner) - 1] == 'D' && output[output_length - 1] == 'E');
  assert(memcmp(output + sizeof(banner), memory + GPS_LOG_START, FLASH_SECTOR_SIZE) == 0);
  FILE *file = fopen("build/test_gps_dump.bin", "wb"); assert(file);
  assert(fwrite(memory + GPS_LOG_START, 1, FLASH_SECTOR_SIZE, file) == FLASH_SECTOR_SIZE);
  fclose(file);
  unsigned saved_writes = writes, saved_erases = erases;
  boot(false);
  assert(logger_status == LOGGER_OCCUPIED && !logger_start());
  pump(50);
  assert(writes == saved_writes && erases == saved_erases);
  dump(); /* Extract after reboot without flashing another program. */

  memory[0] = 0x42; /* Reserved sector is untouched. */
  memory[FLASH_CAPACITY_BYTES - 1] = 0; /* Clear must visit beyond a blank gap. */
  assert(logger_request_clear());
  assert(!logger_start() && !logger_request_dump() && !logger_request_clear());
  pump(20000);
  assert(logger_status == LOGGER_READY && memory[0] == 0x42);
  for (unsigned i = GPS_LOG_START; i < sizeof(memory); ++i) assert(memory[i] == 0xFF);
  assert(logger_stats.accepted_records == 0 && logger_stats.written_records == 0);

  assert(logger_start()); pump(5);
  r = record(10); assert(logger_enqueue(&r));
  clock_ms = UINT32_MAX - 1000U;
  /* Reboot a fresh session with queue age spanning the millisecond wrap. */
  boot(true); assert(logger_start()); pump(5);
  r = record(11); assert(logger_enqueue(&r));
  clock_ms += GPS_LOG_FLUSH_MS;
  pump(5); assert(logger_stats.written_records == 1);
  assert(logger_finish()); pump(10);
  assert(logger_status == LOGGER_COMPLETE);

  boot(true); assert(logger_start()); logger_poll();
  fail_next = true; step(); /* Torn header program. */
  assert(logger_status == LOGGER_ERROR && !logger_is_recording());
  boot(false); assert(logger_status == LOGGER_OCCUPIED && !logger_start());
  dump(); assert(output[output_length - 1] == 'E');
  assert(logger_request_dump());
  logger_dump_poll(send_bytes); logger_dump_poll(send_bytes); /* Start sector read. */
  fail_next = true; step();
  assert(logger_dump_poll(send_bytes));
  assert(output[output_length - 1] == 'X' && logger_status == LOGGER_ERROR);

  boot(true); assert(logger_start()); pump(5);
  memory[GPS_LOG_START + FLASH_SECTOR_SIZE] = 0; /* Existing following sector. */
  for (unsigned i = 0; i < 80 && logger_is_recording(); ++i) {
    r = record(i); (void)logger_enqueue(&r); pump(4);
  }
  pump(10);
  assert(logger_status == LOGGER_OCCUPIED);
  assert(memory[GPS_LOG_START + FLASH_SECTOR_SIZE] == 0);

  boot(true); assert(logger_start()); pump(5);
  /* Reduced-capacity host model exercises the real end-of-device boundary. */
  uint32_t max_records = (FLASH_CAPACITY_BYTES - GPS_LOG_START) / 256U * 4U;
  for (uint32_t i = 0; i < max_records + 8U && logger_is_recording(); ++i) {
    r = record(i); (void)logger_enqueue(&r); pump(4);
  }
  pump(10);
  assert(logger_status == LOGGER_FULL && !logger_start());
  assert(logger_stats.next_address == FLASH_CAPACITY_BYTES);
  assert(!logger_enqueue(&r));
  assert(logger_request_clear());
  for (unsigned i = 0; i < 4; ++i) step(); /* Reset during backwards clearing. */
  boot(false);
  assert(logger_status == LOGGER_OCCUPIED && !logger_start());
  assert(logger_request_clear()); pump(20000);
  assert(logger_status == LOGGER_READY);
  puts("GPS logger tests passed: async copies, buffering, CRC format, end/flush, "
       "dump/backpressure, reboot/torn-write protection, full capacity, "
       "interrupted/full-region clear, wraparound.");
}
