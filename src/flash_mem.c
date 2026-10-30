#include "flash_mem.h"
#include "timer.h"
#include <stddef.h>
#include <string.h>

static bool range_ok(uint32_t address, uint32_t len) {
  return len && address < FLASH_CAPACITY_BYTES &&
         len <= FLASH_CAPACITY_BYTES - address;
}

static bool available(flash_t *flash) {
  return flash != NULL && flash->spi != NULL && flash->initialized &&
         !flash->busy && flash->result == FLASH_RESULT_NONE &&
         !flash->spi->busy && !flash->spi->error &&
         flash->spi->result == SPI_RESULT_NONE;
}

static void finish(flash_t *flash, bool success) {
  if (flash->op == FLASH_OP_PROBE)
    flash->initialized = success;
  flash->op = FLASH_OP_NONE;
  flash->busy = false;
  flash->spi_pending = false;
  flash->result = success ? FLASH_RESULT_OK : FLASH_RESULT_FAILED;
}

static void ready_wait(flash_t *flash, uint32_t timeout) {
  flash->op_start_ms = now_ms();
  flash->deadline_ms = timeout;
  flash->last_status_check_ms = now_ms() - 1U;
  flash->state = FLASH_STATE_CHECK_READY;
}

static void begin(flash_t *flash, flash_op_t op, uint32_t address,
                  uint32_t len) {
  flash->op = op;
  flash->busy = true;
  flash->result = FLASH_RESULT_PENDING;
  flash->address = address;
  flash->remaining = len;
  flash->chunk_len = 0;
  flash->write_data = NULL;
  flash->read_data = NULL;
  flash->spi_pending = false;
  flash->after_write = false;
  ready_wait(flash, 2000U);
}

static void address_command(flash_t *flash, uint8_t command, uint32_t address) {
  flash->tx_buf[0] = command;
  flash->tx_buf[1] = (uint8_t)(address >> 24);
  flash->tx_buf[2] = (uint8_t)(address >> 16);
  flash->tx_buf[3] = (uint8_t)(address >> 8);
  flash->tx_buf[4] = (uint8_t)address;
}

static void submit(flash_t *flash, uint32_t len, flash_state_t next) {
  flash->cs_low();
  if (!spi_transfer_dma(flash->spi, flash->tx_buf, flash->rx_buf, len)) {
    flash->cs_high();
    finish(flash, false);
    return;
  }
  flash->spi_pending = true;
  flash->state = next;
}

void flash_init(flash_t *flash) {
  if (flash == NULL)
    return;
  /* Call once after SPI/GPIO initialization, with no active transfer. */
  flash->initialized = false;
  flash->busy = false;
  flash->op = FLASH_OP_NONE;
  flash->state = FLASH_STATE_IDLE;
  flash->result = FLASH_RESULT_NONE;
  flash->spi_pending = false;
  flash->after_write = false;
  flash->write_data = NULL;
  flash->read_data = NULL;
  flash->address = flash->remaining = flash->chunk_len = 0;
  flash->op_start_ms = flash->last_status_check_ms = 0;
  flash->deadline_ms = flash->verify_address = flash->verify_remaining = 0;
  flash->jedec = 0;
  if (flash->spi == NULL || flash->cs_low == NULL || flash->cs_high == NULL) {
    flash->result = FLASH_RESULT_FAILED;
    return;
  }
  flash->cs_high();
}

bool flash_probe(flash_t *flash) {
  if (flash == NULL || flash->spi == NULL || flash->cs_low == NULL ||
      flash->cs_high == NULL || flash->busy ||
      flash->result != FLASH_RESULT_NONE || flash->spi->busy ||
      flash->spi->error || flash->spi->result != SPI_RESULT_NONE)
    return false;
  flash->initialized = false;
  begin(flash, FLASH_OP_PROBE, 0, 0);
  flash->jedec = 0;
  flash->state = FLASH_STATE_WAKE;
  return true;
}

bool flash_read(flash_t *flash, uint32_t address, uint8_t *data, uint32_t len) {
  if (!data || !range_ok(address, len) || !available(flash))
    return false;
  begin(flash, FLASH_OP_READ, address, len);
  flash->read_data = data;
  return true;
}

bool flash_program(flash_t *flash, uint32_t address, const uint8_t *data,
                   uint32_t len) {
  if (!data || !range_ok(address, len) || !available(flash))
    return false;
  begin(flash, FLASH_OP_PROGRAM, address, len);
  flash->write_data = data;
  return true;
}

bool flash_erase_sector(flash_t *flash, uint32_t address) {
  if (!range_ok(address, FLASH_SECTOR_SIZE) || address % FLASH_SECTOR_SIZE ||
      !available(flash))
    return false;
  begin(flash, FLASH_OP_ERASE, address, FLASH_SECTOR_SIZE);
  return true;
}

void flash_poll(flash_t *flash) {
  if (flash == NULL)
    return;
  if (flash->op == FLASH_OP_NONE)
    return;

  if (flash->spi_pending) {
    /* FAILED may be visible before recovery stops DMA. Wait for safe release.
     */
    if (flash->spi->busy || flash->spi->error)
      return;
    spi_result_t s = spi_take_result(flash->spi);
    if (s == SPI_RESULT_NONE || s == SPI_RESULT_PENDING)
      return;
    flash->cs_high();
    flash->spi_pending = false;
    if (s != SPI_RESULT_OK) {
      finish(flash, false);
      return;
    }
  }

  switch (flash->state) {
  case FLASH_STATE_WAKE:
    flash->tx_buf[0] =
        0xABU; /* Release deep power-down; no reset of active writes. */
    submit(flash, 1, FLASH_STATE_WAKE_DONE);
    break;

  case FLASH_STATE_WAKE_DONE:
    flash->last_status_check_ms = now_ms();
    flash->state = FLASH_STATE_WAKE_DELAY;
    break;

  case FLASH_STATE_WAKE_DELAY:
    if ((uint32_t)(now_ms() - flash->last_status_check_ms) >= 1U)
      ready_wait(flash, 2000U);
    break;

  case FLASH_STATE_CHECK_READY:
    if ((uint32_t)(now_ms() - flash->op_start_ms) >= flash->deadline_ms) {
      finish(flash, false);
      break;
    }
    if ((uint32_t)(now_ms() - flash->last_status_check_ms) < 1U)
      break;
    flash->last_status_check_ms = now_ms();
    flash->tx_buf[0] = 0x05U;
    flash->tx_buf[1] = 0xFFU;
    submit(flash, 2, FLASH_STATE_CHECK_READY_DONE);
    break;

  case FLASH_STATE_CHECK_READY_DONE:
    if (flash->rx_buf[1] & 1U) {
      flash->state = FLASH_STATE_CHECK_READY;
      break;
    }
    if (flash->after_write) {
      flash->verify_address = flash->address;
      flash->verify_remaining =
          flash->op == FLASH_OP_ERASE ? FLASH_SECTOR_SIZE : flash->chunk_len;
      flash->state = FLASH_STATE_VERIFY;
    } else if (flash->op == FLASH_OP_PROBE)
      flash->state = FLASH_STATE_ID;
    else if (flash->op == FLASH_OP_READ)
      flash->state = FLASH_STATE_ISSUE;
    else
      flash->state = FLASH_STATE_WREN;
    break;

  case FLASH_STATE_ID:
    memset(flash->tx_buf, 0xFF, 4);
    flash->tx_buf[0] = 0x9FU;
    submit(flash, 4, FLASH_STATE_ID_DONE);
    break;

  case FLASH_STATE_ID_DONE:
    flash->jedec = ((uint32_t)flash->rx_buf[1] << 16) |
                   ((uint32_t)flash->rx_buf[2] << 8) | flash->rx_buf[3];
    /* Winbond W25Q256JV, 3 V (not a smaller Adafruit W25Q16). */
    finish(flash, flash->jedec == 0xEF4019U);
    break;

  case FLASH_STATE_WREN:
    flash->tx_buf[0] = 0x06U;
    submit(flash, 1, FLASH_STATE_WEL);
    break;

  case FLASH_STATE_WEL:
    flash->tx_buf[0] = 0x05U;
    flash->tx_buf[1] = 0xFFU;
    submit(flash, 2, FLASH_STATE_WEL_DONE);
    break;

  case FLASH_STATE_WEL_DONE:
    if ((flash->rx_buf[1] & 3U) != 2U) {
      finish(flash, false);
      break;
    }
    flash->state = FLASH_STATE_ISSUE;
    break;

  case FLASH_STATE_ISSUE:
    if (flash->op == FLASH_OP_ERASE) {
      address_command(flash, 0x21U,
                      flash->address); /* Dedicated 4-byte sector erase. */
      submit(flash, 5, FLASH_STATE_ISSUE_DONE);
    } else {
      uint32_t room = flash->op == FLASH_OP_PROGRAM
                          ? FLASH_PAGE_SIZE - (flash->address % FLASH_PAGE_SIZE)
                          : FLASH_PAGE_SIZE;
      flash->chunk_len = flash->remaining < room ? flash->remaining : room;
      address_command(flash, flash->op == FLASH_OP_READ ? 0x13U : 0x12U,
                      flash->address);
      if (flash->op == FLASH_OP_PROGRAM)
        memcpy(flash->tx_buf + 5, flash->write_data, flash->chunk_len);
      else
        memset(flash->tx_buf + 5, 0xFF, flash->chunk_len);
      submit(flash, 5U + flash->chunk_len, FLASH_STATE_ISSUE_DONE);
    }
    break;

  case FLASH_STATE_ISSUE_DONE:
    if (flash->op == FLASH_OP_READ) {
      memcpy(flash->read_data, flash->rx_buf + 5, flash->chunk_len);
      flash->read_data += flash->chunk_len;
      flash->address += flash->chunk_len;
      flash->remaining -= flash->chunk_len;
      if (!flash->remaining)
        finish(flash, true);
      else
        flash->state = FLASH_STATE_ISSUE;
    } else {
      flash->after_write = true;
      ready_wait(flash, flash->op == FLASH_OP_ERASE ? 1000U : 50U);
    }
    break;

  case FLASH_STATE_VERIFY: {
    uint32_t n = flash->verify_remaining < FLASH_PAGE_SIZE
                     ? flash->verify_remaining
                     : FLASH_PAGE_SIZE;
    address_command(flash, 0x13U, flash->verify_address);
    memset(flash->tx_buf + 5, 0xFF, n);
    submit(flash, 5U + n, FLASH_STATE_VERIFY_DONE);
    break;
  }

  case FLASH_STATE_VERIFY_DONE: {
    uint32_t n = flash->verify_remaining < FLASH_PAGE_SIZE
                     ? flash->verify_remaining
                     : FLASH_PAGE_SIZE;
    if (flash->op == FLASH_OP_PROGRAM) {
      if (memcmp(flash->rx_buf + 5, flash->write_data, n) != 0) {
        finish(flash, false);
        break;
      }
    } else {
      for (uint32_t i = 0; i < n; ++i) {
        if (flash->rx_buf[5U + i] != 0xFFU) {
          finish(flash, false);
          return;
        }
      }
    }
    flash->verify_address += n;
    flash->verify_remaining -= n;
    if (flash->verify_remaining) {
      flash->state = FLASH_STATE_VERIFY;
      break;
    }
    if (flash->op == FLASH_OP_ERASE) {
      finish(flash, true);
      break;
    }
    flash->write_data += flash->chunk_len;
    flash->address += flash->chunk_len;
    flash->remaining -= flash->chunk_len;
    if (!flash->remaining) {
      finish(flash, true);
      break;
    }
    flash->after_write = false;
    flash->state = FLASH_STATE_WREN;
    break;
  }

  default:
    finish(flash, false);
    break;
  }
}

bool flash_ack_result(flash_t *flash) {
  if (flash == NULL || flash->busy || flash->spi_pending ||
      (flash->result != FLASH_RESULT_OK &&
       flash->result != FLASH_RESULT_FAILED))
    return false;
  flash->result = FLASH_RESULT_NONE;
  flash->state = FLASH_STATE_IDLE;
  return true;
}
