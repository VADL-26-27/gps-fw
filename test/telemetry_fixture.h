#ifndef TELEMETRY_FIXTURE_H
#define TELEMETRY_FIXTURE_H
/* Independent little-endian vector: tow, lat/lon, MSL, N/E/D, H/V accuracy,
 * fix/sats/flags/time-valid, sequence. Shared by TX and receiver tests.
 */
#define TELEMETRY_FIXTURE_HEX \
  "BCCC5B0730048B15008842CC1A9302002EFBFFFF2E160000CCDCFFFF" \
  "BC020000B0040000031983072A000000"
#endif
