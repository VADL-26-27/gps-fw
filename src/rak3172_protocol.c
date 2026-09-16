#include "rak3172_protocol.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int format(char *buf, size_t cap, const char *fmt, ...) {
  if (!buf || !cap) return -1;
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, cap, fmt, args);
  va_end(args);
  if (n < 0 || (size_t)n >= cap) { buf[0] = '\0'; return -1; }
  return n;
}

int rak3172_build_sync(char *buf, size_t cap) {
  return format(buf, cap, "AT\r\n");
}
int rak3172_build_fw_version(char *buf, size_t cap) {
  return format(buf, cap, "AT+VER=?\r\n");
}
int rak3172_build_get_mode(char *buf, size_t cap) {
  return format(buf, cap, "AT+NWM=?\r\n");
}
int rak3172_build_set_mode(char *buf, size_t cap, uint8_t mode) {
  if (mode > 2u) return -1;
  return format(buf, cap, "AT+NWM=%u\r\n", (unsigned)mode);
}
int rak3172_build_set_p2p(char *buf, size_t cap, uint32_t freq, uint8_t sf,
                        uint8_t bandwidth, uint8_t cr, uint16_t preamble,
                        int8_t power) {
  if (freq < 150000000u || freq > 960000000u || sf < 6u || sf > 12u ||
      bandwidth > 9u || cr > 3u || preamble < 2u || power < 5 || power > 22)
    return -1;
  return format(buf, cap, "AT+P2P=%lu:%u:%u:%u:%u:%d\r\n", (unsigned long)freq,
                (unsigned)sf, (unsigned)bandwidth, (unsigned)cr,
                (unsigned)preamble, (int)power);
}
int rak3172_build_precv(char *buf, size_t cap, uint16_t window) {
  return format(buf, cap, "AT+PRECV=%u\r\n", (unsigned)window);
}
int rak3172_build_precv_stop(char *buf, size_t cap) {
  return rak3172_build_precv(buf, cap, 0u);
}
int rak3172_build_psend_hex(char *buf, size_t cap, const uint8_t *bin,
                          size_t bin_len) {
  static const char digits[] = "0123456789ABCDEF";
  if (!buf || !bin || !bin_len || bin_len > RAK3172_MAX_PAYLOAD) return -1;
  size_t len = 9u + bin_len * 2u + 2u;
  if (cap <= len) return -1;
  memcpy(buf, "AT+PSEND=", 9u);
  for (size_t i = 0u; i < bin_len; ++i) {
    buf[9u + 2u * i] = digits[bin[i] >> 4];
    buf[10u + 2u * i] = digits[bin[i] & 15u];
  }
  buf[len - 2u] = '\r';
  buf[len - 1u] = '\n';
  buf[len] = '\0';
  return (int)len;
}

static bool equals(const char *line, size_t len, const char *value) {
  return len == strlen(value) && memcmp(line, value, len) == 0;
}
static bool prefix(const char *line, size_t len, const char *value) {
  size_t n = strlen(value);
  return len >= n && memcmp(line, value, n) == 0;
}
static bool number(const char **p, const char *end) {
  if (*p != end && **p == '-') ++*p;
  const char *start = *p;
  while (*p != end && **p >= '0' && **p <= '9') ++*p;
  return *p != start;
}
static bool rx_packet(const char *line, size_t len) {
  const char *p = line + strlen("+EVT:RXP2P:");
  const char *end = line + len;
  if (!number(&p, end) || p == end || *p++ != ':') return false;
  if (!number(&p, end) || p == end || *p++ != ':') return false;
  size_t hex_len = (size_t)(end - p);
  if (!hex_len || (hex_len & 1u) || hex_len > 2u * RAK3172_MAX_PAYLOAD) return false;
  while (p != end) {
    char c = *p++;
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

rak3172_result_t rak3172_parse_result(const char *line, size_t len) {
  if (!line || !len) return RAK3172_RESULT_UNKNOWN;
  if (equals(line, len, "OK")) return RAK3172_RESULT_OK;
  if (equals(line, len, "ERROR") || equals(line, len, "AT_ERROR") ||
      equals(line, len, "AT_PARAM_ERROR") || equals(line, len, "AT_BUSY_ERROR") ||
      equals(line, len, "AT_MODE_NO_SUPPORT") || equals(line, len, "AT_RX_ERROR") ||
      equals(line, len, "AT_TX_ERROR") || equals(line, len, "AT_TEST_PARAM_OVERFLOW") ||
      equals(line, len, "AT_NO_CLASSB_ENABLE")) return RAK3172_RESULT_ERROR;
  if (equals(line, len, "+EVT:TXP2P DONE")) return RAK3172_RESULT_TX_DONE;
  if (equals(line, len, "+EVT:RXP2P RECEIVE TIMEOUT")) return RAK3172_RESULT_RX_TIMEOUT;
  if (prefix(line, len, "+EVT:RXP2P:"))
    return rx_packet(line, len) ? RAK3172_RESULT_RX_PACKET : RAK3172_RESULT_ERROR;
  if (equals(line, len, "AT+NWM=0")) return RAK3172_RESULT_MODE_P2P;
  if (equals(line, len, "AT+NWM=1") || equals(line, len, "AT+NWM=2"))
    return RAK3172_RESULT_MODE_OTHER;
  if (prefix(line, len, "RAKwireless RAK3172")) return RAK3172_RESULT_BOOT;
  if (prefix(line, len, "+EVT:")) return RAK3172_RESULT_EVT;
  return RAK3172_RESULT_UNKNOWN;
}
