#ifndef TEST_FLASH_MEM_H_
#define TEST_FLASH_MEM_H_
#include <stdbool.h>
#include <stdint.h>
#ifndef FLASH_CAPACITY_BYTES
#define FLASH_CAPACITY_BYTES (32U * 1024U * 1024U)
#endif
#define FLASH_PAGE_SIZE 256U
#define FLASH_SECTOR_SIZE 4096U
typedef enum { FLASH_RESULT_NONE, FLASH_RESULT_PENDING, FLASH_RESULT_OK,
               FLASH_RESULT_FAILED } flash_result_t;
typedef struct flash {
  bool initialized, busy;
  flash_result_t result;
} flash_t;
bool flash_probe(flash_t *flash);
bool flash_read(flash_t *flash, uint32_t address, uint8_t *data, uint32_t length);
bool flash_program(flash_t *flash, uint32_t address, const uint8_t *data, uint32_t length);
bool flash_erase_sector(flash_t *flash, uint32_t address);
bool flash_ack_result(flash_t *flash);
#endif
