# Telemetry and Ground Command Protocol

## GPS telemetry frame

The binary `GPS_FIX` frame is **32 bytes**. It is then hex encoded for RAK3172 `AT+PSEND=<hex>`; only the 32 binary bytes are sent over LoRa P2P.

### `gps_fix_t` payload — 25 bytes

| Field | Type | Bytes | Scaling |
|---|---:|---:|---|
| `timestamp_epoch` | `uint32_t` | 4 | Unix epoch, seconds |
| `nmea_time_utc` | `uint32_t` | 4 | centiseconds since UTC midnight |
| `latitude` | `int32_t` | 4 | degrees × 10⁷ |
| `longitude` | `int32_t` | 4 | degrees × 10⁷ |
| `altitude_msl` | `int16_t` | 2 | meters × 10 |
| `fix_quality` | `uint8_t` | 1 | GGA enum 0–6 |
| `num_sats` | `uint8_t` | 1 | count |
| `hdop` | `uint8_t` | 1 | ×10; clamp above 25.5 |
| `course` | `uint16_t` | 2 | degrees ×10 |
| `speed` | `uint16_t` | 2 | m/s ×100 |

### Frame wrapper — 32 bytes total

| Field | Bytes | Value / purpose |
|---|---:|---|
| `sync` | 2 | Literal `0xAA`, `0x55` |
| `version` | 1 | Layout version; `0x00` is invalid/unset |
| `msg_type` | 1 | `0x01` = `GPS_FIX` |
| `seq_num` | 1 | Rolling 0–255 transmission counter |
| `payload` | 25 | `gps_fix_t` |
| `checksum` | 2 | CRC16-CCITT over `version` through payload; excludes sync |

Serialize every field explicitly in big-endian/network order; never send a raw in-memory C struct. CRC recommendation: CRC-16/CCITT-FALSE (polynomial `0x1021`, initial value `0xFFFF`, no reflection, xorout `0x0000`). Ground software must verify sync, version, type, exact length, and CRC before accepting a frame.

## Ground commands

The command format and allowed command set are **TBD**. Until defined, received ground packets must be logged but must not change radio configuration or flight behavior.

When commands are enabled, define a distinct command type, length, CRC, sequence/replay protection, authentication appropriate to the safety impact, narrow allow-list, acknowledgement/result message, timeout, and rate limit.

`LoRa_Task` receives raw bytes through its DMA ring, parses complete candidate messages, and places them in the bounded `ground_cmd_fifo`. A FIFO is used because command ordering matters; a mailbox is inappropriate because it can overwrite a command.
