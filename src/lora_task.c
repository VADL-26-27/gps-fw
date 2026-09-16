#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lora_task.h"
#include "rak3172_protocol.h"

#define LORA_ACTIVE_TX_LEN 544u
#define LORA_LINE_LEN 600u
#define LORA_RX_BUDGET 256u
#define LORA_COMMAND_MS 500u
#define LORA_UART_TX_MS 200u
#define LORA_BOOT_MS 2000u
#define LORA_RESET_MS 100u
#define LORA_QUIET_MS 100u
#define LORA_RX_WINDOW_MS 3000u
#define LORA_RETRY_LIMIT 3u
#define LORA_BACKOFF_MIN_MS 5000u
#define LORA_BACKOFF_MAX_MS 60000u
#define LORA_HEALTHY_MS 30000u

enum { CONFIG_QUERY_MODE, CONFIG_SET_MODE, CONFIG_STOP_RX, CONFIG_PARAMETERS };
typedef struct {
  lora_state_t state;
  TickType_t deadline;
  TickType_t tx_deadline;
  TickType_t healthy_since;
  uint32_t backoff_ms;
  uint8_t retries;
  uint8_t config_step;
  uint8_t seq;
  bool pending;
  bool ack;
  bool tx_result;
  bool mode_seen;
  bool mode_p2p;
  bool mode_switch_attempted;
  bool rx_ended;
  bool inflight;
  bool healthy;
  bool configured;
  lora_config_t config;
  char line[LORA_LINE_LEN];
  size_t line_len;
  bool discard_line;
} lora_ctx_t;

static TaskHandle_t task_handle;
static volatile lora_state_t state_current = LORA_OFFLINE;
static lora_stats_t stats;
static gps_fix_t latest_fix;
static uint32_t fix_generation;
static bool fix_dirty;
static char active_tx[LORA_ACTIVE_TX_LEN] __attribute__((aligned(4)));

static bool expired(TickType_t deadline, TickType_t now) {
  return (int32_t)(now - deadline) >= 0;
}

static void enter(lora_ctx_t *ctx, lora_state_t state, uint32_t ms) {
  ctx->state = state;
  state_current = state;
  ctx->deadline = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
  ctx->pending = ctx->ack = ctx->tx_result = false;
  ctx->mode_seen = false;
  ctx->rx_ended = false;
}

//can be moved to gps task, place holder while the mailbox doesnt exist
void lora_task_publish_fix(const gps_fix_t *fix) {
  if (!fix) return;
  taskENTER_CRITICAL();
  latest_fix = *fix;
  fix_generation++;
  fix_dirty = true;
  TaskHandle_t task = task_handle;
  taskEXIT_CRITICAL();
  if (task) xTaskNotify(task, LORA_NOTIFY_NEW_FIX, eSetBits);
}

static bool fix_pending(void) {
  taskENTER_CRITICAL();
  bool dirty = fix_dirty;
  taskEXIT_CRITICAL();
  return dirty;
}

lora_state_t lora_task_state_get(void) { return state_current; }
void lora_task_stats_get(lora_stats_t *out) {
  if (!out) return;
  taskENTER_CRITICAL();
  *out = stats;
  taskEXIT_CRITICAL();
}

static void fail(lora_ctx_t *ctx, bool timeout) {
  stats.failures++;
  if (timeout) stats.timeouts++;
  if (ctx->inflight) {
    taskENTER_CRITICAL();
    fix_dirty = true; /* Retry the latest mailbox value, never queue old fixes. */
    taskEXIT_CRITICAL();
    ctx->inflight = false;
  }
  ctx->healthy = false;
  enter(ctx, LORA_RECOVER, 0u);
}

static bool config_valid(const lora_config_t *c) {
  return c && c->frequency_hz >= 150000000u && c->frequency_hz <= 960000000u &&
      c->spreading_factor >= 6u && c->spreading_factor <= 12u &&
      c->bandwidth <= 9u && c->coding_rate <= 3u &&
      c->preamble_symbols >= 2u && c->power_dbm >= 5 && c->power_dbm <= 22 &&
      c->tx_timeout_ms >= LORA_COMMAND_MS && c->tx_timeout_ms <= 60000u;
}

static void clear_parser(lora_ctx_t *ctx) {
  ctx->line_len = 0u;
  ctx->discard_line = false;
}

static bool command_state(lora_state_t state) {
  return state == LORA_UART_SYNC || state == LORA_CONFIGURE_P2P ||
         state == LORA_RX_ARM || state == LORA_RX_STOP ||
         state == LORA_WAIT_TX_RESULT;
}

/* Consume one complete line. An unsolicited event never acknowledges a
 * command. No new command is sent until the entire current RX batch drains. */
static void handle_line(lora_ctx_t *ctx) {
  rak3172_result_t r = rak3172_parse_result(ctx->line, ctx->line_len);
  if (ctx->state == LORA_OFFLINE || ctx->state == LORA_RECOVER ||
      ctx->state == LORA_RESET_ASSERTED || ctx->state == LORA_WAIT_MODULE_REBOOT)
    return;
  if (r == RAK3172_RESULT_BOOT) {
    if (ctx->state == LORA_CONFIGURE_P2P && ctx->config_step == CONFIG_SET_MODE && ctx->pending)
      ctx->ack = true;
    else fail(ctx, false);
    return;
  }
  if (r == RAK3172_RESULT_ERROR) {
    fail(ctx, false);
    return;
  }
  if (r == RAK3172_RESULT_RX_PACKET) {
    /* TBD: ground-station message format/authorization. Count the complete
     * event but do not execute received commands or alter flight behavior. */
    stats.received_packets++;
    if (ctx->state == LORA_RX_ARM && ctx->pending) ctx->rx_ended = true;
    if (ctx->state == LORA_RX_LISTEN) enter(ctx, LORA_PROCESS_RX, 0u);
    return;
  }
  if (r == RAK3172_RESULT_RX_TIMEOUT) {
    if (ctx->state == LORA_RX_ARM && ctx->pending) ctx->rx_ended = true;
    if (ctx->state == LORA_RX_LISTEN) enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS);
    return;
  }
  if (!ctx->pending || !command_state(ctx->state)) return;
  if (r == RAK3172_RESULT_OK) ctx->ack = true;
  if (r == RAK3172_RESULT_TX_DONE && ctx->state == LORA_WAIT_TX_RESULT)
    ctx->tx_result = true;
  if (ctx->state == LORA_CONFIGURE_P2P && ctx->config_step == CONFIG_QUERY_MODE &&
      (r == RAK3172_RESULT_MODE_P2P || r == RAK3172_RESULT_MODE_OTHER)) {
    ctx->mode_seen = true;
    ctx->mode_p2p = r == RAK3172_RESULT_MODE_P2P;
  }
}

/* Preserve incomplete lines, process every complete line, and discard an
 * oversized/binary line through LF before accepting the next line. */
static bool drain_rx(lora_ctx_t *ctx, uart_dma_t *uart) {
  uint8_t bytes[UART_DMA_RX_READ_MAX];
  size_t budget = LORA_RX_BUDGET;
  while (budget) {
    uint16_t n = uart_dma_rx_read(uart, bytes, sizeof(bytes));
    if (!n) break;
    budget -= n;
    for (uint16_t i = 0; i < n; ++i) {
      uint8_t ch = bytes[i];
      if (ch == '\n') {
        if (!ctx->discard_line && ctx->line_len) {
          if (ctx->line[ctx->line_len - 1u] == '\r') ctx->line_len--;
          ctx->line[ctx->line_len] = '\0';
          if (ctx->line_len) handle_line(ctx);
        }
        ctx->line_len = 0u;
        ctx->discard_line = false;
      } else if (!ctx->discard_line) {
        if (ch == 0u || ctx->line_len == sizeof(ctx->line) - 1u) {
          ctx->discard_line = true;
          ctx->line_len = 0u;
          stats.malformed_lines++;
          fail(ctx, false);
        } else ctx->line[ctx->line_len++] = (char)ch;
      }
    }
  }
  return uart_dma_rx_available(uart) != 0u;
}

static void submit(lora_ctx_t *ctx, uart_dma_t *uart, int len, uint32_t timeout_ms) {
  if (len <= 0 || (size_t)len >= sizeof(active_tx) ||
      !uart_dma_start_tx(uart, (const uint8_t *)active_tx, (uint16_t)len)) {
    fail(ctx, false);
    return;
  }
  ctx->pending = true;
  ctx->ack = ctx->tx_result = false;
  ctx->deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  ctx->tx_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(LORA_UART_TX_MS);
}

static void next_config(lora_ctx_t *ctx, uint8_t step) {
  ctx->config_step = step;
  enter(ctx, LORA_CONFIGURE_P2P, LORA_COMMAND_MS);
}

static void go_offline(lora_ctx_t *ctx) {
  enter(ctx, LORA_OFFLINE, ctx->backoff_ms);
  if (ctx->backoff_ms < LORA_BACKOFF_MAX_MS / 2u) ctx->backoff_ms *= 2u;
  else ctx->backoff_ms = LORA_BACKOFF_MAX_MS;
}

/* Run state-entry actions before sleeping. Returns true for another bounded
 * immediate step; commands are serialized through pending + UART ownership. */
static bool step(lora_ctx_t *ctx, uart_dma_t *uart) {
  TickType_t now = xTaskGetTickCount();
  if (ctx->healthy && expired(ctx->healthy_since + pdMS_TO_TICKS(LORA_HEALTHY_MS), now)) {
    ctx->retries = 0u;
    ctx->backoff_ms = LORA_BACKOFF_MIN_MS;
  }
  switch (ctx->state) {
    case LORA_OFFLINE:
      if (!ctx->configured || !expired(ctx->deadline, now)) return false;
      ctx->retries = 0u;
      ctx->mode_switch_attempted = false;
      if (!uart_dma_recover(uart)) { go_offline(ctx); return false; }
      clear_parser(ctx);
      enter(ctx, LORA_WAIT_MODULE_REBOOT, LORA_QUIET_MS);
      return false;
    case LORA_RECOVER:
      clear_parser(ctx);
      if (!uart_dma_stop(uart)) { go_offline(ctx); return false; }
      if (ctx->retries++ < LORA_RETRY_LIMIT) {
        if (!uart_dma_recover(uart)) { go_offline(ctx); return false; }
        enter(ctx, LORA_WAIT_MODULE_REBOOT, LORA_QUIET_MS);
      } else {
        uart_dma_radio_reset(true);
        stats.hardware_resets++;
        ctx->mode_switch_attempted = false;
        enter(ctx, LORA_RESET_ASSERTED, LORA_RESET_MS);
      }
      return false;
    case LORA_RESET_ASSERTED:
      if (!expired(ctx->deadline, now)) return false;
      uart_dma_radio_reset(false);
      /* Backoff exceeds boot time; reception restarts before the next probe. */
      go_offline(ctx);
      return false;
    case LORA_WAIT_MODULE_REBOOT:
      if (!expired(ctx->deadline, now)) return false;
      uart_dma_rx_reset(uart);
      clear_parser(ctx);
      enter(ctx, LORA_UART_SYNC, LORA_COMMAND_MS);
      return true;
    case LORA_PROCESS_RX:
      /* Packet already accounted for by handle_line, before its buffer reuse. */
      enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS);
      return true;
    case LORA_RX_LISTEN:
      if (fix_pending()) enter(ctx, LORA_RX_STOP, LORA_COMMAND_MS);
      else if (expired(ctx->deadline, now)) enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS);
      else return false;
      return true;
    case LORA_SEND_TELEMETRY: {
      if (!fix_pending()) { enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS); return true; }
      if (uart_dma_tx_busy(uart)) {
        if (expired(ctx->deadline, now)) fail(ctx, true);
        return ctx->state == LORA_RECOVER;
      }
      gps_fix_t fix;
      uint32_t generation;
      taskENTER_CRITICAL();
      fix = latest_fix;
      generation = fix_generation;
      taskEXIT_CRITICAL();
      uint8_t frame[TELEMETRY_FRAME_LEN];
      size_t n = telemetry_frame_serialize(&fix, ctx->seq, frame, sizeof(frame));
      if (n != sizeof(frame)) { fail(ctx, false); return true; }
      ctx->seq++; /* README: increment once per completed frame build. */
      int len = rak3172_build_psend_hex(active_tx, sizeof(active_tx), frame, n);
      enter(ctx, LORA_WAIT_TX_RESULT, ctx->config.tx_timeout_ms);
      submit(ctx, uart, len, ctx->config.tx_timeout_ms);
      if (ctx->pending) {
        ctx->inflight = true;
        taskENTER_CRITICAL();
        if (generation == fix_generation) fix_dirty = false;
        taskEXIT_CRITICAL();
      }
      return ctx->state == LORA_RECOVER;
    }
    default: break;
  }

  if (!command_state(ctx->state)) { fail(ctx, false); return true; }
  if (ctx->pending) {
    if (uart_dma_tx_busy(uart)) {
      if (expired(ctx->tx_deadline, now)) { fail(ctx, true); return true; }
      return false;
    }
    if (ctx->ack) {
      switch (ctx->state) {
        case LORA_UART_SYNC: next_config(ctx, CONFIG_QUERY_MODE); return true;
        case LORA_CONFIGURE_P2P:
          switch (ctx->config_step) {
            case CONFIG_QUERY_MODE:
              if (!ctx->mode_seen) break;
              if (ctx->mode_p2p) next_config(ctx, CONFIG_STOP_RX);
              else if (ctx->mode_switch_attempted) fail(ctx, false);
              else next_config(ctx, CONFIG_SET_MODE);
              return true;
            case CONFIG_SET_MODE:
              enter(ctx, LORA_WAIT_MODULE_REBOOT, LORA_BOOT_MS); return false;
            case CONFIG_STOP_RX: next_config(ctx, CONFIG_PARAMETERS); return true;
            case CONFIG_PARAMETERS:
              enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS); return true;
            default: fail(ctx, false); return true;
          }
          break;
        case LORA_RX_ARM:
          if (!ctx->healthy) { ctx->healthy = true; ctx->healthy_since = now; }
          if (ctx->rx_ended) { enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS); return true; }
          enter(ctx, LORA_RX_LISTEN, LORA_RX_WINDOW_MS); return true;
        case LORA_RX_STOP:
          enter(ctx, LORA_SEND_TELEMETRY, LORA_UART_TX_MS); return true;
        case LORA_WAIT_TX_RESULT:
          if (ctx->tx_result) {
            ctx->inflight = false;
            stats.transmitted_frames++;
            enter(ctx, LORA_RX_ARM, LORA_COMMAND_MS); return true;
          }
          break;
        default: break;
      }
    }
    if (expired(ctx->deadline, now)) {
      /* NWM can reboot without an acknowledgement. Re-probe and query mode
       * once; never repeatedly write NWM on every synchronization attempt. */
      if (ctx->state == LORA_CONFIGURE_P2P && ctx->config_step == CONFIG_SET_MODE)
        enter(ctx, LORA_WAIT_MODULE_REBOOT, LORA_BOOT_MS);
      else fail(ctx, true);
      return ctx->state == LORA_RECOVER;
    }
    return false;
  }

  if (uart_dma_tx_busy(uart) || ctx->line_len || ctx->discard_line) {
    if (expired(ctx->deadline, now)) { fail(ctx, true); return true; }
    return false;
  }
  int len = -1;
  switch (ctx->state) {
    case LORA_UART_SYNC: len = rak3172_build_sync(active_tx, sizeof(active_tx)); break;
    case LORA_CONFIGURE_P2P:
      switch (ctx->config_step) {
        case CONFIG_QUERY_MODE: len = rak3172_build_get_mode(active_tx, sizeof(active_tx)); break;
        case CONFIG_SET_MODE:
          ctx->mode_switch_attempted = true;
          len = rak3172_build_set_mode(active_tx, sizeof(active_tx), 0u); break;
        case CONFIG_STOP_RX: len = rak3172_build_precv_stop(active_tx, sizeof(active_tx)); break;
        case CONFIG_PARAMETERS:
          len = rak3172_build_set_p2p(active_tx, sizeof(active_tx),
              ctx->config.frequency_hz, ctx->config.spreading_factor,
              ctx->config.bandwidth, ctx->config.coding_rate,
              ctx->config.preamble_symbols, ctx->config.power_dbm); break;
        default: break;
      }
      break;
    case LORA_RX_ARM:
      len = rak3172_build_precv(active_tx, sizeof(active_tx), LORA_RX_WINDOW_MS); break;
    case LORA_RX_STOP: len = rak3172_build_precv_stop(active_tx, sizeof(active_tx)); break;
    default: break;
  }
  submit(ctx, uart, len, LORA_COMMAND_MS);
  return ctx->state == LORA_RECOVER;
}

static TickType_t wait_ticks(const lora_ctx_t *ctx, const uart_dma_t *uart) {
  TickType_t now = xTaskGetTickCount();
  if (ctx->state == LORA_OFFLINE && !ctx->configured) return pdMS_TO_TICKS(1000u);
  int32_t remaining = (int32_t)(ctx->deadline - now);
  TickType_t wait = remaining > 0 ? (TickType_t)remaining : 1u;
  if (ctx->pending && uart_dma_tx_busy(uart)) {
    remaining = (int32_t)(ctx->tx_deadline - now);
    if (remaining <= 0) return 1u;
    if (remaining > 0 && (TickType_t)remaining < wait) wait = (TickType_t)remaining;
  }
  return wait ? wait : 1u;
}

static bool service(lora_ctx_t *ctx, uart_dma_t *uart, uint32_t bits) {
  /* Notifications are wakeups, not durable state. In particular, an ERROR
   * queued before restart must not poison a freshly recovered UART. */
  (void)bits;
  bool active = ctx->state != LORA_OFFLINE && ctx->state != LORA_RESET_ASSERTED &&
                ctx->state != LORA_RECOVER;
  if (active && uart_dma_faulted(uart))
    fail(ctx, false);
  bool more_rx = false;
  if (active && ctx->state != LORA_RECOVER) more_rx = drain_rx(ctx, uart);
  if (active && uart_dma_faulted(uart) && ctx->state != LORA_RECOVER) fail(ctx, false);
  if (more_rx && command_state(ctx->state)) {
    TickType_t now = xTaskGetTickCount();
    if (expired(ctx->deadline, now) ||
        (ctx->pending && uart_dma_tx_busy(uart) && expired(ctx->tx_deadline, now)))
      fail(ctx, true);
  }
  if (!more_rx || ctx->state == LORA_RECOVER) {
    for (unsigned i = 0; i < 8u && step(ctx, uart); ++i) {
      /* Drain trailing replies before issuing a different command. */
      if (uart_dma_rx_available(uart)) { more_rx = true; break; }
    }
  }
  return more_rx;
}

void LoRa_Task(void *argument) {
  lora_ctx_t ctx = {0};
  uart_dma_t *uart = uart_dma_lora_get();
  ctx.configured = config_valid(argument);
  if (ctx.configured) ctx.config = *(const lora_config_t *)argument;
  ctx.backoff_ms = LORA_BACKOFF_MIN_MS;
  taskENTER_CRITICAL();
  task_handle = xTaskGetCurrentTaskHandle();
  taskEXIT_CRITICAL();
  uart_dma_set_notify_task(uart, task_handle);
  uart_dma_radio_control_init();
  bool initialized = uart_dma_lora_init(uart);
  /* No selected RF configuration means no radio commands or resets. */
  if (!ctx.configured && initialized) (void)uart_dma_stop(uart);
  enter(&ctx, LORA_OFFLINE, 0u);
  uint32_t bits = 0u;
  for (;;) {
    bool more_rx = service(&ctx, uart, bits);
    bits = 0u;
    /* A one-tick yield bounds work even under a continuous unsolicited stream.
     * Notifications are hints; RX availability and the mailbox are persistent. */
    xTaskNotifyWait(0u, UINT32_MAX, &bits, more_rx ? 1u : wait_ticks(&ctx, uart));
  }
}
