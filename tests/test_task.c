#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/lora_task.c"

static TickType_t now;
static uart_dma_t uart;
static char rx[4096], sent[544];
static size_t rx_in, rx_out;
static unsigned sends, stops, restarts;
static bool reset_asserted, refuse_tx, refuse_recover;
static ground_rx_record_t captured[4];
static unsigned captured_count;
static bool sink_full;

bool ground_rx_fifo_try_push(const ground_rx_record_t *record) {
  if (sink_full) return false;
  assert(captured_count < 4);
  captured[captured_count++] = *record;
  return true;
}

TickType_t xTaskGetTickCount(void) { return now; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &uart; }
BaseType_t xTaskNotify(TaskHandle_t t, uint32_t b, eNotifyAction a) {
  (void)t; (void)b; (void)a; return pdTRUE;
}
BaseType_t xTaskNotifyFromISR(TaskHandle_t t, uint32_t b, eNotifyAction a, BaseType_t *w) {
  (void)w; return xTaskNotify(t, b, a);
}
BaseType_t xTaskNotifyWait(uint32_t a, uint32_t b, uint32_t *v, TickType_t ticks) {
  (void)a; (void)b; *v = 0; now += ticks; return pdFALSE;
}
uart_dma_t *uart_dma_lora_get(void) { return &uart; }
bool uart_dma_lora_init(uart_dma_t *u) { (void)u; return true; }
void uart_dma_set_notify_task(uart_dma_t *u, TaskHandle_t t) { u->notify_task = t; }
bool uart_dma_start_rx(uart_dma_t *u) { (void)u; return true; }
uint16_t uart_dma_rx_available(uart_dma_t *u) { (void)u; return (uint16_t)(rx_in - rx_out); }
uint16_t uart_dma_rx_read(uart_dma_t *u, uint8_t *dst, uint16_t max) {
  uint16_t n = uart_dma_rx_available(u);
  if (n > max) n = max;
  if (n > UART_DMA_RX_READ_MAX) n = UART_DMA_RX_READ_MAX;
  memcpy(dst, rx + rx_out, n); rx_out += n; return n;
}
void uart_dma_rx_reset(uart_dma_t *u) { (void)u; rx_in = rx_out = 0; }
bool uart_dma_start_tx(uart_dma_t *u, const uint8_t *buf, uint16_t n) {
  assert(!u->tx_busy);
  assert(n < sizeof(sent));
  if (refuse_tx) return false;
  memcpy(sent, buf, n); sent[n] = 0; sends++; u->tx_busy = true; return true;
}
bool uart_dma_tx_busy(const uart_dma_t *u) { return u->tx_busy; }
bool uart_dma_faulted(const uart_dma_t *u) { return u->fault; }
uint32_t uart_dma_error_count(const uart_dma_t *u) { return u->error_count; }
bool uart_dma_stop(uart_dma_t *u) { stops++; u->tx_busy = false; rx_in = rx_out = 0; return true; }
bool uart_dma_recover(uart_dma_t *u) {
  restarts++; u->tx_busy = u->fault = false; rx_in = rx_out = 0; return !refuse_recover;
}
void uart_dma_radio_control_init(void) { }
void uart_dma_radio_reset(bool asserted) { reset_asserted = asserted; }

static void feed(const char *s) {
  size_t n = strlen(s);
  if (rx_in == rx_out) rx_in = rx_out = 0;
  assert(rx_in + n <= sizeof(rx));
  memcpy(rx + rx_in, s, n); rx_in += n;
}
static void tick(lora_ctx_t *ctx) { (void)service(ctx, &uart, 0u); }
static void response(lora_ctx_t *ctx, const char *s) {
  uart.tx_busy = false;
  feed(s); tick(ctx);
}
static void setup(lora_ctx_t *ctx) {
  memset(ctx, 0, sizeof(*ctx)); memset(&uart, 0, sizeof(uart));
  memset(&stats, 0, sizeof(stats)); memset(active_tx, 0, sizeof(active_tx));
  now = 0; rx_in = rx_out = sends = stops = restarts = 0;
  fix_dirty = false; fix_generation = 0; task_handle = NULL;
  reset_asserted = refuse_tx = refuse_recover = false;
  captured_count = 0; sink_full = false;
  ctx->config = (lora_config_t){868000000, 7, 0, 0, 8, 14, 5000};
  ctx->configured = true; ctx->backoff_ms = LORA_BACKOFF_MIN_MS;
  enter(ctx, LORA_OFFLINE, 0);
}
static void sync_ready(lora_ctx_t *ctx) {
  tick(ctx); assert(ctx->state == LORA_WAIT_MODULE_REBOOT);
  now += LORA_QUIET_MS; tick(ctx);
  assert(ctx->state == LORA_UART_SYNC && ctx->pending);
  assert(strcmp(sent, "AT\r\n") == 0);
  assert(ctx->deadline == now + LORA_COMMAND_MS);
}
static void listening(lora_ctx_t *ctx) {
  sync_ready(ctx);
  response(ctx, "OK\r\n"); assert(strcmp(sent, "AT+NWM=?\r\n") == 0);
  response(ctx, "AT+NWM=0\r\nOK\r\n"); assert(strcmp(sent, "AT+PRECV=0\r\n") == 0);
  response(ctx, "OK\r\n"); assert(strncmp(sent, "AT+P2P=", 7) == 0);
  response(ctx, "OK\r\n"); assert(strcmp(sent, "AT+PRECV=3000\r\n") == 0);
  response(ctx, "OK\r\n"); assert(ctx->state == LORA_RX_LISTEN || ctx->state == LORA_RX_STOP);
}

int main(void) {
  lora_ctx_t c;
  gps_fix_t fix = {.latitude = 123};
  setup(&c);
  c.configured = false; tick(&c);
  assert(c.state == LORA_OFFLINE && !sends && !restarts);
  assert(!config_valid(NULL));
  assert(config_valid(&c.config));
  c.config.frequency_hz = 0; assert(!config_valid(&c.config));

  /* Entry action runs now; fragmented input survives across task wakes. */
  setup(&c); sync_ready(&c);
  uart.tx_busy = false; feed("O"); tick(&c);
  assert(c.state == LORA_UART_SYNC && c.line_len == 1);
  feed("K\r\n"); tick(&c); assert(c.state == LORA_CONFIGURE_P2P);

  /* An unsolicited event cannot acknowledge a command. */
  response(&c, "+EVT:TXP2P DONE\r\n+EVT:RXP2P RECEIVE TIMEOUT\r\n");
  assert(c.state == LORA_CONFIGURE_P2P && !c.ack);

  /* Every trailing response is drained before issuing the next command. */
  setup(&c); sync_ready(&c);
  response(&c, "OK\r\nOK\r\n");
  assert(c.state == LORA_CONFIGURE_P2P && !c.ack);
  assert(sends == 2);

  /* UART still owns the buffer even if an early radio reply arrived. */
  setup(&c); sync_ready(&c);
  char before[544]; memcpy(before, active_tx, sizeof(before));
  feed("OK\r\n"); tick(&c);
  assert(sends == 1 && memcmp(before, active_tx, sizeof(before)) == 0);
  uart.tx_busy = false; tick(&c); assert(sends == 2);

  /* Mode change followed by reboot then query; no nonexistent third command. */
  setup(&c); sync_ready(&c); response(&c, "OK\r\n");
  response(&c, "AT+NWM=1\r\nOK\r\n"); assert(strcmp(sent, "AT+NWM=0\r\n") == 0);
  response(&c, "OK\r\nRAKwireless RAK3172\r\n");
  assert(c.state == LORA_WAIT_MODULE_REBOOT);
  now += LORA_BOOT_MS; tick(&c); assert(strcmp(sent, "AT\r\n") == 0);
  response(&c, "OK\r\n"); response(&c, "AT+NWM=0\r\nOK\r\n");
  assert(strcmp(sent, "AT+PRECV=0\r\n") == 0);

  /* Pre-start fixes persist. Newest fix wins while previous TX is active. */
  setup(&c); lora_task_publish_fix(&fix); listening(&c); tick(&c);
  assert(c.state == LORA_RX_STOP && fix_pending());
  response(&c, "OK\r\n");
  assert(c.state == LORA_WAIT_TX_RESULT && !fix_pending());
  assert(strncmp(sent, "AT+PSEND=AA55010100", 18) == 0);
  response(&c, "OK\r\n");
  assert(c.state == LORA_WAIT_TX_RESULT && stats.transmitted_frames == 0);
  fix.latitude = 456; lora_task_publish_fix(&fix);
  response(&c, "+EVT:UNRELATED\r\n"); assert(c.state == LORA_WAIT_TX_RESULT);
  response(&c, "+EVT:TXP2P DONE\r\n");
  assert(c.state == LORA_RX_ARM && stats.transmitted_frames == 1 && fix_pending());

  /* If radio result fails, retry the latest fix after recovery. */
  setup(&c); listening(&c); lora_task_publish_fix(&fix); tick(&c);
  response(&c, "OK\r\n"); response(&c, "AT_BUSY_ERROR\r\n");
  assert(c.state == LORA_WAIT_MODULE_REBOOT && fix_pending() && stops);

  /* Busy TX and rejected starts cannot drop a pending fix or rewrite DMA data. */
  setup(&c); lora_task_publish_fix(&fix); enter(&c, LORA_SEND_TELEMETRY, 200);
  uart.tx_busy = true; strcpy(active_tx, "owned"); tick(&c);
  assert(fix_pending() && strcmp(active_tx, "owned") == 0 && sends == 0);
  uart.tx_busy = false; refuse_tx = true; tick(&c);
  assert(fix_pending() && stats.failures == 1);

  /* Coalesced arm acknowledgement + packet must rearm, not falsely listen. */
  setup(&c); listening(&c);
  enter(&c, LORA_RX_ARM, 500); tick(&c);
  response(&c, "OK\r\n+EVT:RXP2P:-90:4:AA55\r\n");
  assert(stats.received_packets == 1 && c.state == LORA_RX_ARM && c.pending);
  assert(captured_count == 1 && captured[0].length == 2);
  assert(captured[0].payload[0] == 0xaa && captured[0].payload[1] == 0x55);
  assert(stats.rx_queued == 1 && stats.rx_dropped == 0);

  /* Fragmented packets and consecutive lines preserve binary data/order. */
  setup(&c); listening(&c);
  feed("+EVT:RXP2P:-90:4:00"); tick(&c);
  assert(captured_count == 0);
  feed("ffAa\r\n+EVT:RXP2P:-80:5:1234\r\n"); tick(&c);
  assert(captured_count == 2 && stats.rx_queued == 2);
  assert(captured[0].length == 3 && captured[0].payload[0] == 0);
  assert(captured[0].payload[1] == 0xff && captured[0].payload[2] == 0xaa);
  assert(captured[1].length == 2 && captured[1].payload[0] == 0x12);
  assert(captured[1].payload[1] == 0x34);

  /* An unavailable/full sink drops data without interrupting reception. */
  setup(&c); listening(&c); sink_full = true;
  feed("+EVT:RXP2P:-90:4:AA55\r\n"); tick(&c);
  assert(stats.rx_dropped == 1 && stats.rx_queued == 0 && !captured_count);
  assert(c.state == LORA_RX_ARM && stats.failures == 0);
  response(&c, "OK\r\n"); assert(c.state == LORA_RX_LISTEN);
  feed("+EVT:RXP2P:-90:4:GG\r\n"); tick(&c);
  assert(stats.received_packets == 1 && !captured_count);

  /* More than one budget of bytes drains without requiring more interrupts. */
  setup(&c); listening(&c);
  for (unsigned i = 0; i < 40; ++i) feed("+EVT:UNRELATED\r\n");
  unsigned passes = 0;
  while (uart_dma_rx_available(&uart)) { tick(&c); assert(++passes < 10); }
  assert(passes > 1 && c.state == LORA_RX_LISTEN);

  /* Oversized lines and receive errors recover instead of parsing suffixes. */
  setup(&c); listening(&c);
  char long_line[800]; memset(long_line, 'A', sizeof(long_line));
  long_line[798] = '\n'; long_line[799] = 0; feed(long_line);
  for (unsigned i = 0; i < 4; ++i) tick(&c);
  assert(stats.malformed_lines == 1 && stats.failures == 1 && stops);
  setup(&c); listening(&c);
  uart.fault = true;
  service(&c, &uart, UART_DMA_NOTIFY_ERROR);
  assert(c.state == LORA_WAIT_MODULE_REBOOT && stats.failures == 1);
  service(&c, &uart, UART_DMA_NOTIFY_ERROR);
  assert(stats.failures == 1); /* Stale error notification after restart. */

  /* UART completion and AT reply both have finite, independent deadlines. */
  setup(&c); sync_ready(&c); now += LORA_UART_TX_MS; tick(&c);
  assert(stats.timeouts == 1 && stops);
  setup(&c); sync_ready(&c); uart.tx_busy = false;
  now += LORA_COMMAND_MS; tick(&c); assert(stats.timeouts == 1);
  setup(&c); sync_ready(&c);
  for (unsigned i = 0; i < 40; ++i) feed("+EVT:UNRELATED\r\n");
  now += LORA_UART_TX_MS; tick(&c);
  assert(stats.timeouts == 1 && stops); /* Flood cannot hide a stuck UART. */

  /* Missing mode-set OK is a possible reboot; a real ERROR is not. */
  setup(&c); sync_ready(&c); response(&c, "OK\r\n");
  response(&c, "AT+NWM=1\r\nOK\r\n");
  uart.tx_busy = false; now += LORA_COMMAND_MS; tick(&c);
  assert(c.state == LORA_WAIT_MODULE_REBOOT && stats.failures == 0);
  setup(&c); sync_ready(&c); response(&c, "OK\r\n");
  response(&c, "AT+NWM=1\r\nOK\r\n");
  response(&c, "AT_PARAM_ERROR\r\n");
  assert(stats.failures == 1 && stops);

  /* No repeated reset loop when the UART itself cannot restart. */
  setup(&c); refuse_recover = true; tick(&c);
  assert(c.state == LORA_OFFLINE && c.deadline == now + 5000 && !reset_asserted);

  /* Repeated failures reach a timed hardware reset and increasing backoff. */
  setup(&c);
  for (unsigned i = 0; i <= LORA_RETRY_LIMIT; ++i) { fail(&c, true); tick(&c); }
  assert(c.state == LORA_RESET_ASSERTED && reset_asserted);
  now += LORA_RESET_MS - 1; tick(&c); assert(reset_asserted);
  now++; tick(&c);
  assert(!reset_asserted && c.state == LORA_OFFLINE && c.deadline == now + 5000);
  now = c.deadline; tick(&c); assert(c.state == LORA_WAIT_MODULE_REBOOT);
  for (unsigned i = 0; i <= LORA_RETRY_LIMIT; ++i) { fail(&c, true); tick(&c); }
  now += LORA_RESET_MS; tick(&c); assert(c.deadline == now + 10000);
  assert(stats.hardware_resets == 2);

  /* Tick rollover does not extend deadlines or force a one-tick polling loop. */
  setup(&c); now = UINT32_MAX - 10u; enter(&c, LORA_UART_SYNC, 500);
  tick(&c); assert(wait_ticks(&c, &uart) == LORA_UART_TX_MS);
  uart.tx_busy = false; assert(wait_ticks(&c, &uart) == LORA_COMMAND_MS);
  uart.tx_busy = false; now += 499u; tick(&c); assert(stats.failures == 0);
  now++; tick(&c); assert(stats.timeouts == 1);
  puts("task state-machine tests passed");
}
