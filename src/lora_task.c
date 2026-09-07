#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lora_task.h"
#include "rak3172_protocol.h"

#define LORA_ACTIVE_TX_LEN 544u
#define LORA_RESP_BUF_LEN 160u
#define LORA_P2P_FREQ_HZ 915000000u
#define LORA_P2P_SF 7u
#define LORA_P2P_BW_HZ 125000u
#define LORA_P2P_CR 1u
#define LORA_P2P_PREAMBLE 8u
#define LORA_P2P_POWER 14

#define LORA_RETRY_LIMIT 3u

#define LORA_OFFLINE_BACKOFF_MS 5000u
#define LORA_SYNC_TIMEOUT_MS 500u
#define LORA_CONFIG_TIMEOUT_MS 500u
#define LORA_REBOOT_TIMEOUT_MS 2000u
#define LORA_PRECV_TIMEOUT_MS 500u
#define LORA_RX_WINDOW_MS 3000u
#define LORA_TX_WAIT_TIMEOUT_MS 1000u

typedef struct {
  lora_state_t state;
  TickType_t deadline;
  uint8_t retries;
  uint8_t config_step;
  uint16_t seq;
  bool cmd_pending;
  bool fix_pending;
} lora_ctx_t;

typedef struct {
  gps_fix_t fix[2];
  uint8_t read_slot;
  uint8_t write_slot;
} lora_fix_mailbox_t;

static TaskHandle_t task_handle;
static volatile lora_state_t state_current = LORA_OFFLINE;
static lora_fix_mailbox_t fix_mailbox;
static uint8_t lora_active_tx[LORA_ACTIVE_TX_LEN] __attribute__((aligned(4)));
static char resp_buf[LORA_RESP_BUF_LEN];
static size_t resp_len;

static void lora_set_state(lora_ctx_t *ctx, lora_state_t state,
                           uint32_t timeout_ms) {
  ctx->state = state;
  ctx->deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  ctx->cmd_pending = false;
  state_current = state;
}

static bool lora_timed_out(const lora_ctx_t *ctx) {
  return (int32_t)(ctx->deadline - xTaskGetTickCount()) <= 0;
}

static TickType_t lora_wait_ticks(const lora_ctx_t *ctx) {
  TickType_t now = xTaskGetTickCount();
  if (ctx->deadline <= now) {
    return 1u;
  }
  return ctx->deadline - now;
}

static void lora_resp_reset(void) {
  resp_len = 0u;
  resp_buf[0] = '\0';
}

static rak3172_result_t lora_collect_response(uart_dma_t *uart) {
  if (resp_len >= LORA_RESP_BUF_LEN - 1u) {
    lora_resp_reset();
  }
  uint16_t avail =
      uart_dma_rx_read(uart, (uint8_t *)&resp_buf[resp_len],
                       (uint16_t)(LORA_RESP_BUF_LEN - 1u - resp_len));
  resp_len += avail;
  resp_buf[resp_len] = '\0';

  rak3172_result_t result = rak3172_parse_result(resp_buf, resp_len);
  if (result != RAK3172_RESULT_UNKNOWN) {
    lora_resp_reset();
  }
  return result;
}

static void lora_start_command(lora_ctx_t *ctx, uart_dma_t *uart, int len) {
  if (len > 0 && !uart_dma_tx_busy(uart)) {
    uart_dma_start_tx(uart, lora_active_tx, (uint16_t)len);
    ctx->cmd_pending = true;
  }
}

static void lora_goto_recover(lora_ctx_t *ctx) {
  lora_set_state(ctx, LORA_RECOVER, 1u);
}

void lora_task_publish_fix(const gps_fix_t *fix) {
  if (fix == NULL) {
    return;
  }
  taskENTER_CRITICAL();
  fix_mailbox.fix[fix_mailbox.write_slot] = *fix;
  fix_mailbox.read_slot = fix_mailbox.write_slot;
  fix_mailbox.write_slot ^= 1u;
  taskEXIT_CRITICAL();
  if (task_handle != NULL) {
    xTaskNotify(task_handle, LORA_NOTIFY_NEW_FIX, eSetBits);
  }
}

static void lora_fix_snapshot(gps_fix_t *out) {
  taskENTER_CRITICAL();
  *out = fix_mailbox.fix[fix_mailbox.read_slot];
  taskEXIT_CRITICAL();
}

lora_state_t lora_task_state_get(void) {
  return state_current;
}

static void lora_run(uart_dma_t *uart, lora_ctx_t *ctx, uint32_t *bits) {
  if ((*bits) & LORA_NOTIFY_NEW_FIX) {
    ctx->fix_pending = true;
    (*bits) &= ~LORA_NOTIFY_NEW_FIX;
  }

  switch (ctx->state) {
    case LORA_OFFLINE:
      if (lora_timed_out(ctx)) {
        ctx->retries = 0u;
        lora_set_state(ctx, LORA_UART_SYNC, LORA_SYNC_TIMEOUT_MS);
      }
      break;

    case LORA_UART_SYNC: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        result = RAK3172_RESULT_ERROR;
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
      }
      if (!ctx->cmd_pending) {
        lora_start_command(ctx, uart,
                           rak3172_build_sync(lora_active_tx,
                                              LORA_ACTIVE_TX_LEN));
      }
      if (result == RAK3172_RESULT_OK) {
        ctx->config_step = 0u;
        lora_set_state(ctx, LORA_CONFIGURE_P2P, LORA_CONFIG_TIMEOUT_MS);
      } else if (result == RAK3172_RESULT_ERROR || lora_timed_out(ctx)) {
        lora_goto_recover(ctx);
      }
      break;
    }

    case LORA_CONFIGURE_P2P: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        result = RAK3172_RESULT_ERROR;
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
      }
      if (!ctx->cmd_pending) {
        int len = -1;
        if (ctx->config_step == 0u) {
          len = rak3172_build_set_mode(lora_active_tx, LORA_ACTIVE_TX_LEN,
                                       ctx->config_step);
          if (len <= 0) {
            len = rak3172_build_set_p2p(lora_active_tx, LORA_ACTIVE_TX_LEN,
                                        LORA_P2P_FREQ_HZ, LORA_P2P_SF,
                                        LORA_P2P_BW_HZ, LORA_P2P_CR,
                                        LORA_P2P_PREAMBLE, LORA_P2P_POWER);
          }
        } else if (ctx->config_step == 1u) {
          len = rak3172_build_set_p2p(lora_active_tx, LORA_ACTIVE_TX_LEN,
                                      LORA_P2P_FREQ_HZ, LORA_P2P_SF,
                                      LORA_P2P_BW_HZ, LORA_P2P_CR,
                                      LORA_P2P_PREAMBLE, LORA_P2P_POWER);
        }
        lora_start_command(ctx, uart, len);
      }
      if (result == RAK3172_RESULT_OK || result == RAK3172_RESULT_EVT) {
        if (ctx->config_step >= 2u) {
          ctx->retries = 0u;
          lora_set_state(ctx, LORA_RX_ARM, LORA_PRECV_TIMEOUT_MS);
        } else {
          ctx->config_step++;
          lora_set_state(ctx, LORA_CONFIGURE_P2P, LORA_CONFIG_TIMEOUT_MS);
        }
      } else if (result == RAK3172_RESULT_ERROR) {
        lora_set_state(ctx, LORA_WAIT_MODULE_REBOOT, LORA_REBOOT_TIMEOUT_MS);
      } else if (lora_timed_out(ctx)) {
        lora_goto_recover(ctx);
      }
      break;
    }

    case LORA_WAIT_MODULE_REBOOT:
      if ((*bits) & (UART_DMA_NOTIFY_RX_DATA | UART_DMA_NOTIFY_ERROR)) {
        (*bits) &= ~(UART_DMA_NOTIFY_RX_DATA | UART_DMA_NOTIFY_ERROR);
        lora_resp_reset();
        lora_set_state(ctx, LORA_UART_SYNC, LORA_SYNC_TIMEOUT_MS);
      } else if (lora_timed_out(ctx)) {
        lora_set_state(ctx, LORA_UART_SYNC, LORA_SYNC_TIMEOUT_MS);
      }
      break;

    case LORA_RX_ARM: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        result = RAK3172_RESULT_ERROR;
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
      }
      if (!ctx->cmd_pending) {
        lora_start_command(ctx, uart,
                           rak3172_build_precv(lora_active_tx,
                                               LORA_ACTIVE_TX_LEN,
                                               LORA_RX_WINDOW_MS));
      }
      if (result == RAK3172_RESULT_OK || result == RAK3172_RESULT_EVT) {
        ctx->retries = 0u;
        lora_set_state(ctx, LORA_RX_LISTEN, LORA_RX_WINDOW_MS);
      } else if (result == RAK3172_RESULT_ERROR || lora_timed_out(ctx)) {
        lora_goto_recover(ctx);
      }
      break;
    }

    case LORA_RX_LISTEN: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
        lora_goto_recover(ctx);
        break;
      }
      if (result != RAK3172_RESULT_UNKNOWN) {
        lora_set_state(ctx, LORA_PROCESS_RX, 1u);
      } else if (ctx->fix_pending) {
        lora_set_state(ctx, LORA_RX_STOP, LORA_PRECV_TIMEOUT_MS);
      } else if (lora_timed_out(ctx)) {
        lora_set_state(ctx, LORA_RX_ARM, LORA_PRECV_TIMEOUT_MS);
      }
      break;
    }

    case LORA_PROCESS_RX:
      (void)lora_collect_response(uart);
      lora_resp_reset();
      lora_set_state(ctx, LORA_RX_ARM, LORA_PRECV_TIMEOUT_MS);
      break;

    case LORA_RX_STOP: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        result = RAK3172_RESULT_ERROR;
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
      }
      if (!ctx->cmd_pending) {
        lora_start_command(ctx, uart,
                           rak3172_build_precv_stop(lora_active_tx,
                                                    LORA_ACTIVE_TX_LEN));
      }
      if (result == RAK3172_RESULT_OK || result == RAK3172_RESULT_EVT) {
        lora_set_state(ctx, LORA_SEND_TELEMETRY, 1u);
      } else if (result == RAK3172_RESULT_ERROR || lora_timed_out(ctx)) {
        lora_goto_recover(ctx);
      }
      break;
    }

    case LORA_SEND_TELEMETRY: {
      if (!ctx->fix_pending) {
        lora_set_state(ctx, LORA_RX_ARM, LORA_PRECV_TIMEOUT_MS);
        break;
      }
      gps_fix_t fix;
      uint8_t frame[TELEMETRY_FRAME_LEN];
      lora_fix_snapshot(&fix);
      size_t frame_len =
          telemetry_frame_serialize(&fix, (uint8_t)ctx->seq, frame,
                                    (size_t)sizeof(frame));
      if (frame_len == (size_t)TELEMETRY_FRAME_LEN) {
        ctx->seq++;
        ctx->fix_pending = false;
      }
      int len = rak3172_build_psend_hex(lora_active_tx, LORA_ACTIVE_TX_LEN,
                                        frame, frame_len);
      if (len <= 0) {
        lora_goto_recover(ctx);
        break;
      }
      if (!ctx->cmd_pending && !uart_dma_tx_busy(uart)) {
        uart_dma_start_tx(uart, lora_active_tx, (uint16_t)len);
        ctx->cmd_pending = true;
        lora_set_state(ctx, LORA_WAIT_TX_RESULT, LORA_TX_WAIT_TIMEOUT_MS);
        ctx->cmd_pending = true;
      }
      break;
    }

    case LORA_WAIT_TX_RESULT: {
      rak3172_result_t result = RAK3172_RESULT_UNKNOWN;
      if ((*bits) & UART_DMA_NOTIFY_RX_DATA) {
        result = lora_collect_response(uart);
        (*bits) &= ~UART_DMA_NOTIFY_RX_DATA;
      }
      if ((*bits) & UART_DMA_NOTIFY_ERROR) {
        result = RAK3172_RESULT_ERROR;
        (*bits) &= ~UART_DMA_NOTIFY_ERROR;
      }
      if (result == RAK3172_RESULT_OK || result == RAK3172_RESULT_EVT) {
        ctx->retries = 0u;
        lora_set_state(ctx, LORA_RX_ARM, LORA_PRECV_TIMEOUT_MS);
      } else if (result == RAK3172_RESULT_ERROR || lora_timed_out(ctx)) {
        lora_goto_recover(ctx);
      }
      break;
    }

    case LORA_RECOVER:
      lora_resp_reset();
      uart_dma_rx_reset(uart);
      if (ctx->retries < LORA_RETRY_LIMIT) {
        ctx->retries++;
        lora_set_state(ctx, LORA_UART_SYNC, LORA_SYNC_TIMEOUT_MS);
      } else {
        lora_set_state(ctx, LORA_OFFLINE, LORA_OFFLINE_BACKOFF_MS);
      }
      break;
  }
}

void LoRa_Task(void *argument) {
  (void)argument;
  lora_ctx_t ctx;
  uint32_t bits = 0u;
  uart_dma_t *uart = uart_dma_lora_get();

  memset(&ctx, 0, sizeof(ctx));

  task_handle = xTaskGetCurrentTaskHandle();
  uart_dma_set_notify_task(uart, task_handle);
  uart_dma_lora_init(uart);
  uart_dma_start_rx(uart);

  lora_resp_reset();
  lora_set_state(&ctx, LORA_OFFLINE, LORA_OFFLINE_BACKOFF_MS);

  for (;;) {
    bits |= ulTaskNotifyTake(pdTRUE, lora_wait_ticks(&ctx));
    lora_run(uart, &ctx, &bits);
    bits = 0u;
  }
}