#ifndef LOGGING_CONTROL_H_
#define LOGGING_CONTROL_H_

#include <stdbool.h>

/* RAM-only latch: false after boot/reset, true after BEGIN_LOG on USART2.
 * There is no stop command and this state is never saved to external flash.
 */
extern volatile bool logging_enabled;

void logging_control_init(void);
void logging_control_poll(void);

#endif
