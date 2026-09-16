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
  LORA_RESET_ASSERTED,
} lora_state_t;

/* TBD: frequency, SF, bandwidth, coding rate, preamble, TX power and an
 * airtime-appropriate TX-result timeout. No flight RF defaults are selected.
 * Pass a fully specified configuration to LoRa_Task; NULL leaves it offline.
 * RUI3 AT+P2P units: bandwidth index 0..9, coding-rate index 0..3. */
typedef struct {
  uint32_t frequency_hz;
  uint8_t spreading_factor;
  uint8_t bandwidth;
  uint8_t coding_rate;
  uint16_t preamble_symbols;
  int8_t power_dbm;
  uint32_t tx_timeout_ms;
} lora_config_t;

typedef struct {
  uint32_t failures;
  uint32_t timeouts;
  uint32_t malformed_lines;
  uint32_t received_packets;
  uint32_t transmitted_frames;
  uint32_t hardware_resets;
} lora_stats_t;

#define LORA_NOTIFY_NEW_FIX (1u << 3)

void LoRa_Task(void *argument);
/* Task context only; retains fixes published before LoRa_Task starts. */
void lora_task_publish_fix(const gps_fix_t *fix);
lora_state_t lora_task_state_get(void);
void lora_task_stats_get(lora_stats_t *out);
#endif
