#include "rak3172_protocol.h"

int rak3172_build_sync(char *buf, size_t cap) {
  (void)buf;
  (void)cap;
  return -1;
}

int rak3172_build_fw_version(char *buf, size_t cap) {
  (void)buf;
  (void)cap;
  return -1;
}

int rak3172_build_set_mode(char *buf, size_t cap, uint8_t mode) {
  (void)buf;
  (void)cap;
  (void)mode;
  return -1;
}

int rak3172_build_set_p2p(char *buf, size_t cap, uint32_t freq, uint8_t sf,
                          uint32_t bw, uint8_t cr, uint8_t preamble,
                          int8_t power) {
  (void)buf;
  (void)cap;
  (void)freq;
  (void)sf;
  (void)bw;
  (void)cr;
  (void)preamble;
  (void)power;
  return -1;
}

int rak3172_build_precv(char *buf, size_t cap, uint16_t window) {
  (void)buf;
  (void)cap;
  (void)window;
  return -1;
}

int rak3172_build_precv_stop(char *buf, size_t cap) {
  (void)buf;
  (void)cap;
  return -1;
}

int rak3172_build_psend_hex(char *buf, size_t cap, const uint8_t *bin,
                            size_t bin_len) {
  (void)buf;
  (void)cap;
  (void)bin;
  (void)bin_len;
  return -1;
}

rak3172_result_t rak3172_parse_result(const char *line, size_t len) {
  (void)line;
  (void)len;
  return RAK3172_RESULT_UNKNOWN;
}