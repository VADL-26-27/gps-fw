#ifndef TEST_USART_BOARD_H
#define TEST_USART_BOARD_H
#include <stdint.h>
#include <stdbool.h>
#define BUFFER_SIZE 1024U
typedef struct {
  volatile uint32_t rx_error_count;
  volatile uint32_t rx_drop_count;
} usart_t;
extern usart_t uart5;
extern usart_t uart2;
bool usart_write(usart_t *port, const uint8_t *data, uint32_t len);
bool usart_read(usart_t *port, uint8_t *data, uint32_t len);
#endif
