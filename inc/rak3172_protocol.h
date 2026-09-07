#ifndef RAK3172_PROTOCOL_H
#define RAK3172_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  RAK3172_RESULT_UNKNOWN = 0,
  RAK3172_RESULT_OK,
  RAK3172_RESULT_ERROR,
  RAK3172_RESULT_EVT,
} rak3172_result_t;

int rak3172_build_sync(char *buf, size_t cap);
int rak3172_build_fw_version(char *buf, size_t cap);
int rak3172_build_set_mode(char *buf, size_t cap, uint8_t mode);
int rak3172_build_set_p2p(char *buf, size_t cap, uint32_t freq, uint8_t sf,
                          uint32_t bw, uint8_t cr, uint8_t preamble,
                          int8_t power);
int rak3172_build_precv(char *buf, size_t cap, uint16_t window);
int rak3172_build_precv_stop(char *buf, size_t cap);
int rak3172_build_psend_hex(char *buf, size_t cap, const uint8_t *bin,
                            size_t bin_len);
rak3172_result_t rak3172_parse_result(const char *line, size_t len);

#endif