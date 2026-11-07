#ifndef LOGGING_CONTROL_H_
#define LOGGING_CONTROL_H_

#include <stdbool.h>

/* BEGIN_LOG enables recording only if the flash logger is ready. LOG_END stops
 * accepting records and flushes them. Reset always disables recording.
 * LOG_DUMP, LOG_CLEAR and LOG_STATUS are also accepted, CR/LF terminated.
 */
extern volatile bool logging_enabled;

void logging_control_init(void);
void logging_control_poll(void);

#endif
