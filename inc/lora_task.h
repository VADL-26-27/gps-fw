#ifndef LORA_TASK_H
#define LORA_TASK_H

#include <stdint.h>

#include "telemetry_frame.h"
#include "uart_dma_driver.h"

typedef enum {
  LORA_OFFLINE = 0,
  LORA_UART_SYNC,
  LORA_CONFIGURE_P2P,
  LORA_WAIT_MODULE_REBOOT,
  LORA_RX_ARM,
  LORA_RX_LISTEN,
  LORA_PROCESS_RX,
  LORA_RX_STOP,
  LORA_SEND_TELEMETRY,
  LORA_WAIT_TX_RESULT,
  LORA_RECOVER,
} lora_state_t;

#define LORA_NOTIFY_NEW_FIX (1u << 3)

void LoRa_Task(void *argument);
void lora_task_publish_fix(const gps_fix_t *fix);
lora_state_t lora_task_state_get(void);

#endif