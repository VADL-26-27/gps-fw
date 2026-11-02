#include "lora.h"
#include "usart_board.h"
#include "timer.h"
#include <string.h>

static const uint8_t hex[] = "0123456789ABCDEF";
static const uint8_t prefix[] = "AT+PSEND=";

volatile lora_tx_status_t rak_tx_status = LORA_TX_IDLE;
volatile uint32_t rak_attempt_count;
volatile uint32_t rak_ok_count;
volatile uint32_t rak_tx_confirmed_count;
volatile uint32_t rak_timeout_count;
volatile uint32_t rak_error_count;
volatile bool rak_ok;
const char *volatile rak_last_command;
char rak_last_response[128];
uint8_t rak_rx_raw[128];
volatile uint32_t rak_rx_raw_count;

typedef enum {
  RADIO_OFF,
  RADIO_BOOT_WAIT,
  RADIO_RESET_WAIT,
  RADIO_CONFIG_SEND,
  RADIO_CONFIG_WAIT,
  RADIO_MODE_WAIT,
  RADIO_READY,
  RADIO_RETRY_WAIT
} radio_state_t;

typedef enum {
  CONFIG_AT,
  CONFIG_READ_MODE,
  CONFIG_SET_MODE,
  CONFIG_DISABLE_RX,
  CONFIG_DISABLE_CAD,
  CONFIG_P2P
} config_step_t;

static const char *const config_commands[] = {
    "AT\r\n", "AT+NWM=?\r\n", "AT+NWM=0\r\n", "AT+PRECV=0\r\n",
    "AT+CAD=0\r\n", LORA_P2P_COMMAND};

static radio_state_t state;
static config_step_t config_step;
static uint32_t started_ms;
static uint32_t rx_errors;
static uint32_t rx_drops;
static uint32_t line_length;
static bool discard_line;
static bool tx_done_pending;
static int mode_value;
static uint32_t mode_switches;
static char line[128];
static char last_command[128];

static void clear_reply(void) {
  line_length = 0U;
  discard_line = false;
  rak_last_response[0] = '\0';
  rak_rx_raw_count = 0U;
  rx_errors = uart5.rx_error_count;
  rx_drops = uart5.rx_drop_count;
}

/* usart_write copies the command into its TX queue before returning. */
static bool queue_command(const uint8_t *command, uint32_t length) {
  if (length >= sizeof(last_command) ||
      !usart_write(&uart5, command, length)) {
    return false;
  }
  memcpy(last_command, command, length);
  last_command[length] = '\0';
  rak_last_command = last_command;
  clear_reply();
  started_ms = now_ms();
  return true;
}

bool lora_tx_busy(void) {
  return rak_tx_status == LORA_TX_WAIT_OK ||
         rak_tx_status == LORA_TX_WAIT_DONE;
}

bool lora_is_ready(void) { return state == RADIO_READY; }

void lora_init(void) {
  /* ATZ below resets the module as well as the STM32-side protocol state.
   * This prevents a late TX event from an old session confirming a new send.
   */
  state = RADIO_BOOT_WAIT;
  started_ms = now_ms();
  config_step = CONFIG_AT;
  mode_value = -1;
  mode_switches = 0U;
  rak_tx_status = LORA_TX_IDLE;
  rak_ok = false;
  clear_reply();
}

static void fail_radio(bool timeout) {
  if (timeout) {
    ++rak_timeout_count;
  } else {
    ++rak_error_count;
  }
  if (lora_tx_busy()) {
    rak_tx_status = timeout ? LORA_TX_TIMEOUT : LORA_TX_ERROR;
  }
  /* Do not immediately reuse the link: replies have no transaction ID.
   * Reset/reconfigure before accepting another packet after a failure.
   * The failed packet is not automatically retransmitted.
   */
  state = RADIO_RETRY_WAIT;
  started_ms = now_ms();
}

static void next_config(config_step_t next) {
  config_step = next;
  state = RADIO_CONFIG_SEND;
  started_ms = now_ms();
}

static bool is_error(const char *text) {
  return strcmp(text, "ERROR") == 0 ||
         strncmp(text, "AT_", 3U) == 0 ||
         strncmp(text, "+EVT:TXP2P FAIL", 15U) == 0;
}

static void handle_line(void) {
  if (state == RADIO_CONFIG_WAIT) {
    if (is_error(line)) {
      fail_radio(false);
      return;
    }
    if (config_step == CONFIG_READ_MODE) {
      const char *value = line;
      if (strncmp(value, "AT+NWM=", 7U) == 0) {
        value += 7U;
      }
      if (value[0] >= '0' && value[0] <= '2' && value[1] == '\0') {
        mode_value = value[0] - '0';
      }
    }
    if (strcmp(line, "OK") != 0) {
      return;
    }
    switch (config_step) {
    case CONFIG_AT:
      next_config(CONFIG_READ_MODE);
      break;
    case CONFIG_READ_MODE:
      if (mode_value == 0) {
        next_config(CONFIG_DISABLE_RX);
      } else if (mode_value > 0 && mode_switches == 0U) {
        next_config(CONFIG_SET_MODE);
      } else {
        fail_radio(false);
      }
      break;
    case CONFIG_DISABLE_RX:
      next_config(CONFIG_DISABLE_CAD);
      break;
    case CONFIG_DISABLE_CAD:
      next_config(CONFIG_P2P);
      break;
    case CONFIG_P2P:
      state = RADIO_READY;
      break;
    default:
      break;
    }
    return;
  }

  if (state == RADIO_MODE_WAIT && is_error(line)) {
    fail_radio(false);
    return;
  }
  if (state != RADIO_READY || !lora_tx_busy()) {
    return; /* Ignore boot text and unsolicited/duplicate replies. */
  }
  if (is_error(line)) {
    fail_radio(false);
  } else if (strcmp(line, "OK") == 0 &&
             rak_tx_status == LORA_TX_WAIT_OK) {
    rak_ok = true;
    ++rak_ok_count;
    rak_tx_status = LORA_TX_WAIT_DONE;
    started_ms = now_ms();
  } else if (strcmp(line, "+EVT:TXP2P DONE") == 0 &&
             rak_tx_status == LORA_TX_WAIT_DONE) {
    /* Commit only after checking UART errors for the entire received batch. */
    tx_done_pending = true;
  }
}

static void receive_byte(uint8_t byte) {
  uint32_t count = rak_rx_raw_count;
  if (count < sizeof(rak_rx_raw)) {
    rak_rx_raw[count] = byte;
  }
  if (count < sizeof(rak_last_response) - 1U) {
    rak_last_response[count] = (char)byte;
    rak_last_response[count + 1U] = '\0';
  }
  ++rak_rx_raw_count;

  if (byte == '\r') {
    return;
  }
  if (byte == '\n') {
    if (!discard_line && line_length != 0U) {
      line[line_length] = '\0';
      handle_line();
    }
    line_length = 0U;
    discard_line = false;
  } else if (byte == '\0' || line_length >= sizeof(line) - 1U) {
    /* Discard the whole malformed line, including any trailing "OK". */
    discard_line = true;
  } else if (!discard_line) {
    line[line_length++] = (char)byte;
  }
}

void lora_poll(void) {
  uint8_t byte;
  bool expecting_reply = state == RADIO_CONFIG_WAIT || lora_tx_busy();
  tx_done_pending = false;
  /* Bound work even if the UART is flooded. No waits for future bytes. */
  for (uint32_t budget = BUFFER_SIZE; budget != 0U; --budget) {
    if (!usart_read(&uart5, &byte, 1U)) {
      break;
    }
    receive_byte(byte);
  }

  if (expecting_reply &&
      (rx_errors != uart5.rx_error_count || rx_drops != uart5.rx_drop_count)) {
    fail_radio(false);
  }
  if (tx_done_pending && state == RADIO_READY &&
      rak_tx_status == LORA_TX_WAIT_DONE) {
    ++rak_tx_confirmed_count;
    rak_tx_status = LORA_TX_CONFIRMED;
  }

  uint32_t elapsed = (uint32_t)(now_ms() - started_ms);
  switch (state) {
  case RADIO_BOOT_WAIT:
    if (elapsed >= 3000U && queue_command((const uint8_t *)"ATZ\r\n", 5U)) {
      state = RADIO_RESET_WAIT;
    }
    break;
  case RADIO_RESET_WAIT:
    if (elapsed >= 3000U) {
      next_config(CONFIG_AT);
    }
    break;
  case RADIO_CONFIG_SEND: {
    const char *command = config_commands[config_step];
    if (queue_command((const uint8_t *)command, (uint32_t)strlen(command))) {
      mode_value = -1;
      if (config_step == CONFIG_SET_MODE) {
        ++mode_switches;
        state = RADIO_MODE_WAIT; /* Changing NWM may reboot without an OK. */
      } else {
        state = RADIO_CONFIG_WAIT;
      }
    } else if (elapsed >= LORA_COMMAND_TIMEOUT_MS) {
      fail_radio(true);
    }
    break;
  }
  case RADIO_MODE_WAIT:
    if (elapsed >= 3000U) {
      next_config(CONFIG_READ_MODE);
    }
    break;
  case RADIO_CONFIG_WAIT:
    if (elapsed >= LORA_COMMAND_TIMEOUT_MS) {
      fail_radio(true);
    }
    break;
  case RADIO_READY:
    if ((rak_tx_status == LORA_TX_WAIT_OK &&
         elapsed >= LORA_COMMAND_TIMEOUT_MS) ||
        (rak_tx_status == LORA_TX_WAIT_DONE &&
         elapsed >= LORA_TX_TIMEOUT_MS)) {
      fail_radio(true);
    }
    break;
  case RADIO_RETRY_WAIT:
    if (elapsed >= 5000U) {
      lora_init();
    }
    break;
  default:
    break;
  }
}

bool transmit_packet(const gps_transmit_t *packet) {
  if (packet == NULL) {
    return false;
  }
  lora_poll(); /* Consume old replies before starting a new transaction. */
  if (!lora_is_ready() || lora_tx_busy()) {
    return false;
  }

  const uint8_t *data = (const uint8_t *)packet;

  uint8_t command[sizeof(prefix) - 1U + 2U * sizeof(*packet) + 2U];
  uint32_t length = 0U;

  memcpy(command, prefix, sizeof(prefix) - 1U);
  length = sizeof(prefix) - 1U;

  for (uint32_t i = 0U; i < sizeof(*packet); ++i) {
    command[length++] = hex[data[i] >> 4];
    command[length++] = hex[data[i] & 0x0FU];
  }

  command[length++] = '\r';
  command[length++] = '\n';

  if (!queue_command(command, length)) {
    return false;
  }
  rak_ok = false;
  rak_tx_status = LORA_TX_WAIT_OK;
  ++rak_attempt_count;
  return true;
}
