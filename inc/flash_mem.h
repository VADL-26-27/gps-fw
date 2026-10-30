#ifndef FLASH_MEM_H_
#define FLASH_MEM_H_

#define FLASH_CAPACITY_BYTES (32U * 1024U * 1024U)
#define FLASH_PAGE_SIZE 256U
#define FLASH_SECTOR_SIZE 4096U

#include <stdint.h>
#include "spi.h"
#include "timer.h"
#include <stddef.h>

typedef enum {
  FLASH_RESULT_NONE,
  FLASH_RESULT_PENDING,
  FLASH_RESULT_OK,
  FLASH_RESULT_FAILED
} flash_result_t;

typedef enum {
  FLASH_OP_NONE,
  FLASH_OP_PROBE,
  FLASH_OP_READ,
  FLASH_OP_PROGRAM,
  FLASH_OP_ERASE
} flash_op_t;

typedef enum {
  FLASH_STATE_IDLE,
  FLASH_STATE_WAKE,
  FLASH_STATE_WAKE_DONE,
  FLASH_STATE_WAKE_DELAY,
  FLASH_STATE_CHECK_READY,
  FLASH_STATE_CHECK_READY_DONE,
  FLASH_STATE_ID,
  FLASH_STATE_ID_DONE,
  FLASH_STATE_WREN,
  FLASH_STATE_WEL,
  FLASH_STATE_WEL_DONE,
  FLASH_STATE_ISSUE,
  FLASH_STATE_ISSUE_DONE,
  FLASH_STATE_VERIFY,
  FLASH_STATE_VERIFY_DONE
} flash_state_t;

typedef struct {
  spi_t *spi;

  void (*cs_low)(void);
  void (*cs_high)(void);

  flash_state_t state;
  flash_op_t op;
  flash_result_t result;

  bool busy;
  bool spi_pending; // Waiting for a submitted SPI transfer
  bool initialized;
  bool after_write;

  uint32_t address;   // Current operation address
  uint32_t remaining; // Bytes left
  uint32_t chunk_len; // Current page/read chunk

  const uint8_t *write_data;
  uint8_t *read_data;

  uint32_t op_start_ms;
  uint32_t last_status_check_ms;
  uint32_t deadline_ms;

  uint32_t verify_address;
  uint32_t verify_remaining;
  uint32_t jedec;

  // Persistent buffers: DMA must not use expired local arrays.
  uint8_t tx_buf[261]; // Command + 4-byte address + 256 data bytes
  uint8_t rx_buf[261];
} flash_t;

void flash_init(flash_t *flash);

bool flash_probe(flash_t *flash);

bool flash_read(flash_t *flash, uint32_t address, uint8_t *data, uint32_t len);
/* Erased destination required; split at pages and verify by readback. */

bool flash_program(flash_t *flash, uint32_t address, const uint8_t *data,
                   uint32_t len);

/* Address must be 4 KiB aligned. Full sector readback verification follows. */
bool flash_erase_sector(flash_t *flash, uint32_t address);

void flash_poll(flash_t *flash);

bool flash_ack_result(flash_t *flash);

#endif // FLASH_MEM_H_