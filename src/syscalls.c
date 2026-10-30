#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "stm32f413xx.h"
#include <stdint.h>
/* Called by __libc_init_array() from the vendor startup file. */
void _init(void) {}

// int _write(int file, char *data, int len)
// {
//     (void)file;

//     for (int i = 0; i < len; ++i) {
//         while (!(USART2->SR & USART_SR_TXE)) {}
//         USART2->DR = (uint8_t)data[i];
//     }

//     return len;
// }