#ifndef LORA_H_
#define LORA_H_

#include "gps.h"

/* RUI3 LoRa P2P settings. The receiving radio must use matching settings. */
#define LORA_P2P_COMMAND "AT+P2P=915000000:12:0:3:8:22\r\n"
#define LORA_COMMAND_TIMEOUT_MS 2000U
#define LORA_TX_TIMEOUT_MS 5000U
#define LORA_NO_LOCK_MESSAGE "NO LOCK FOUND"

typedef enum {
  LORA_TX_IDLE,
  LORA_TX_WAIT_OK,
  LORA_TX_WAIT_DONE,
  LORA_TX_CONFIRMED,
  LORA_TX_ERROR,
  LORA_TX_TIMEOUT
} lora_tx_status_t;

/* Call after board_usart_init(). Resets/reconfigures the RAK asynchronously.
 * Call these functions only from the main loop; this module owns UART5 RX.
 */
void lora_init(void);
void lora_poll(void);
bool lora_is_ready(void);
bool lora_tx_busy(void);

/* True means queued, not transmitted. Poll until rak_tx_status becomes
 * CONFIRMED, ERROR, or TIMEOUT. No second packet is accepted while busy.
 * CONFIRMED means local RF TX finished; it is not a remote receiver ACK.
 */
bool transmit_packet(const gps_transmit_t *packet);
/* Sends exactly the ASCII status text, without a trailing NUL or newline.
 * Uses the same readiness, busy, timeout and TX confirmation handling. */
bool transmit_no_lock(void);

/* Debugger watches. Counters are cumulative, including across lora_init(). */
extern volatile lora_tx_status_t rak_tx_status;
extern volatile uint32_t rak_attempt_count;
extern volatile uint32_t rak_ok_count; /* TX commands acknowledged with OK. */
extern volatile uint32_t rak_tx_confirmed_count;
extern volatile uint32_t rak_timeout_count;
extern volatile uint32_t rak_error_count;
extern volatile bool rak_ok;
extern const char *volatile rak_last_command;
extern char rak_last_response[128];
extern uint8_t rak_rx_raw[128];
extern volatile uint32_t rak_rx_raw_count;

#endif // LORA_H_
