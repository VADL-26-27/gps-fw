// Heltec WiFi LoRa 32 V4 receiver. RadioLib 7.4.0+ (built with 7.8.1).
// Receives 44-byte GPS telemetry or ASCII NO LOCK FOUND from STM32 / RAK3172.
// Open the serial monitor at 115200 baud.
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "telemetry_decode.h"

#define LORA_NSS 8
#define LORA_SCK 9
#define LORA_MOSI 10
#define LORA_MISO 11
#define LORA_RST 12
#define LORA_BUSY 13
#define LORA_DIO1 14
#define LORA_FEM_EN 2

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
volatile bool receivedFlag = false;
TelemetrySequenceTracker sequences;
uint32_t goodPackets = 0, crcErrors = 0, invalidPackets = 0;
uint32_t noLockPackets = 0;

void ARDUINO_ISR_ATTR onReceive() { receivedFlag = true; }

void checkRadio(const char *operation, int result) {
  if (result == RADIOLIB_ERR_NONE)
    return;
  Serial.printf("%s failed: %d\n", operation, result);
  while (true)
    delay(100);
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000)
    delay(10);

  pinMode(LORA_FEM_EN, OUTPUT);
  digitalWrite(LORA_FEM_EN, HIGH);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // 915 MHz, BW 125 kHz, SF12, CR 4/8, private sync word,
  // preamble 8 symbols, TCXO 1.8 V. Power is unused during reception.
  checkRadio("LoRa init",
             radio.begin(915.0f, 125.0f, 12, 8,
                         RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 2, 8, 1.8f, false));
  checkRadio("RF switch", radio.setDio2AsRfSwitch(true));
  radio.setDio1Action(onReceive);
  checkRadio("Start receive", radio.startReceive());
  Serial.println("LoRa receiver ready: 915 MHz, SF12, BW125, CR4/8");
}

void loop() {
  if (!receivedFlag) {
    delay(1);
    return;
  }
  receivedFlag = false;

  uint8_t payload[255];
  size_t length = radio.getPacketLength();
  // SX1262 LoRa payloads are at most 255 bytes.
  if (length > sizeof(payload)) {
    Serial.printf("Invalid packet length: %u\n", (unsigned)length);
    checkRadio("Restart receive", radio.startReceive());
    return;
  }
  int result = radio.readData(payload, length);
  float rssi = radio.getRSSI();
  float snr = radio.getSNR();
  checkRadio("Restart receive", radio.startReceive());

  if (result == RADIOLIB_ERR_CRC_MISMATCH) {
    ++crcErrors;
    Serial.printf("Radio CRC error; packet discarded (total=%lu)\n",
                  (unsigned long)crcErrors);
    return;
  }
  if (result != RADIOLIB_ERR_NONE) {
    Serial.printf("Receive error: %d\n", result);
    return;
  }

  if (isNoLockMessage(payload, length)) {
    ++noLockPackets;
    Serial.printf("NO LOCK FOUND | RSSI=%.1f dBm | SNR=%.1f dB | Status packets=%lu\n",
                  rssi, snr, (unsigned long)noLockPackets);
    return;
  }

  DecodedTelemetry fix;
  if (!decodeTelemetry(payload, length, fix)) {
    ++invalidPackets;
    Serial.printf("Invalid GPS payload: %u bytes (total=%lu)\n",
                  (unsigned)length, (unsigned long)invalidPackets);
    return;
  }
  ++goodPackets;
  SequenceObservation observation = sequences.observe(fix.sequence);
  Serial.printf("\nSeq=%lu | GNSS tow=%lu ms | RSSI=%.1f dBm | SNR=%.1f dB\n",
                (unsigned long)fix.sequence, (unsigned long)fix.i_tow_ms, rssi,
                snr);
  Serial.printf("Lat=%.7f Lon=%.7f Altitude MSL=%.3f m\n",
                fix.latitude_e7 * 1e-7, fix.longitude_e7 * 1e-7,
                fix.altitude_msl_mm * 0.001);
  Serial.printf(
      "Accuracy H/V=%.3f/%.3f m | Velocity N/E/D=%.3f/%.3f/%.3f m/s\n",
      fix.horizontal_accuracy_mm * 0.001, fix.vertical_accuracy_mm * 0.001,
      fix.velocity_north_mm_s * 0.001, fix.velocity_east_mm_s * 0.001,
      fix.velocity_down_mm_s * 0.001);
  Serial.printf("Fix=%u Satellites=%u Flags=0x%02X TimeFlags=0x%02X\n",
                (unsigned)fix.fix_type, (unsigned)fix.satellites_used,
                (unsigned)fix.fix_flags, (unsigned)fix.time_valid_flags);
  if (observation.kind == SequenceKind::Duplicate) {
    Serial.println("Duplicate record sequence.");
  } else if (observation.kind == SequenceKind::RestartOrReordered) {
    Serial.println(
        "Sequence went backward: restart or reordering; new baseline.");
  } else if (observation.gap != 0U) {
    Serial.printf(
        "Gap: %lu record(s) not received (includes transmitter skips).\n",
        (unsigned long)observation.gap);
  }
  Serial.printf("Good=%lu Unreceived=%lu Duplicates=%lu CRC errors=%lu\n",
                (unsigned long)goodPackets,
                (unsigned long)sequences.unreceived_records,
                (unsigned long)sequences.duplicates, (unsigned long)crcErrors);
}
