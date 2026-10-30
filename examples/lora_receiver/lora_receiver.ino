// Heltec WiFi LoRa 32 V4 receiver. Requires RadioLib 7.4.0.
// Matches the LoRa settings in the GPS / LoRa coexistence transmitter.
// Open the serial monitor at 115200 baud.
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

#define LORA_NSS     8
#define LORA_SCK     9
#define LORA_MOSI    10
#define LORA_MISO    11
#define LORA_RST     12
#define LORA_BUSY    13
#define LORA_DIO1    14
#define LORA_FEM_EN  2

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
volatile bool receivedFlag = false;

void ARDUINO_ISR_ATTR onReceive() {
  receivedFlag = true;
}

void checkRadio(const char* operation, int result) {
  if (result == RADIOLIB_ERR_NONE) return;
  Serial.printf("%s failed: %d\n", operation, result);
  while (true) delay(100);
}

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  pinMode(LORA_FEM_EN, OUTPUT);
  digitalWrite(LORA_FEM_EN, HIGH);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // 915 MHz, BW 125 kHz, SF7, CR 4/5, private sync word,
  // preamble 8 symbols, TCXO 1.8 V. Power is unused during reception.
  checkRadio("LoRa init", radio.begin(915.0f, 125.0f, 7, 5,
             RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 2, 8, 1.8f, false));
  checkRadio("RF switch", radio.setDio2AsRfSwitch(true));
  radio.setDio1Action(onReceive);
  checkRadio("Start receive", radio.startReceive());
  Serial.println("LoRa receiver ready: 915 MHz, SF7, BW125, CR4/5");
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

  if (result != RADIOLIB_ERR_NONE && result != RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.printf("Receive error: %d\n", result);
    return;
  }

  Serial.printf("\nRX %u bytes | RSSI %.1f dBm | SNR %.1f dB%s\n",
                (unsigned)length, rssi, snr,
                result == RADIOLIB_ERR_CRC_MISMATCH ? " | CRC ERROR" : "");
  Serial.print("RAW: ");
  Serial.write(payload, length);  // Includes embedded zero bytes.
  Serial.println();
  Serial.print("HEX: ");
  for (size_t i = 0; i < length; ++i) {
    Serial.printf("%02X ", (unsigned)payload[i]);
  }
  Serial.println();
}
