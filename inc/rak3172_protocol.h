#ifndef RAK3172_PROTOCOL_H
#define RAK3172_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define RAK3172_MAX_PAYLOAD 255u

typedef enum {
  RAK3172_RESULT_UNKNOWN = 0,
  RAK3172_RESULT_OK,
  RAK3172_RESULT_ERROR,
  RAK3172_RESULT_EVT,
  RAK3172_RESULT_TX_DONE,
  RAK3172_RESULT_RX_PACKET,
  RAK3172_RESULT_RX_TIMEOUT,
  RAK3172_RESULT_MODE_P2P,
  RAK3172_RESULT_MODE_OTHER,
  RAK3172_RESULT_BOOT,
} rak3172_result_t;

/* RUI3 AT dialect. Builders return byte length excluding NUL, or -1.
 * All commands include CRLF; cap must also have room for a trailing NUL. */
int rak3172_build_sync(char *buf, size_t cap);
int rak3172_build_fw_version(char *buf, size_t cap);
int rak3172_build_get_mode(char *buf, size_t cap);
int rak3172_build_set_mode(char *buf, size_t cap, uint8_t mode);
/* bandwidth and cr are RUI3 indices, not Hz or a coding-rate denominator. */
int rak3172_build_set_p2p(char *buf, size_t cap, uint32_t freq, uint8_t sf,
                        uint8_t bandwidth, uint8_t cr, uint16_t preamble,
                        int8_t power);
int rak3172_build_precv(char *buf, size_t cap, uint16_t window);
int rak3172_build_precv_stop(char *buf, size_t cap);
int rak3172_build_psend_hex(char *buf, size_t cap, const uint8_t *bin,
                          size_t bin_len);
/* Classify exactly one complete line, without CRLF. No substring matching. */
rak3172_result_t rak3172_parse_result(const char *line, size_t len);
#endif
