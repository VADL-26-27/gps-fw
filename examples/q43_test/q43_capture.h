#ifndef Q43_CAPTURE_H_
#define Q43_CAPTURE_H_

#include <Arduino.h>

// Forward the Q43 UART output unchanged, including CR/LF and checksums.
// Call repeatedly from loop(). No parsing or configuration commands.
// gnss and console must be separate serial interfaces.
inline uint32_t captureQ43(Stream &gnss, Print &console) {
  uint32_t received = 0;
  // Limit work per call so the rest of loop() can also run.
  for (unsigned int count = 0; count < 256 && gnss.available() > 0; ++count) {
    const int byte = gnss.read();
    if (byte < 0) {
      break;
    }
    ++received;
    console.write(static_cast<uint8_t>(byte));
  }
  return received;
}

#endif // Q43_CAPTURE_H_
