#include <Arduino.h>
#include <string.h>

// STM32duino 3.x Uart constructor order: RX, TX.
Uart Rak(PB12, PB13);                 // UART5: RAK TX -> PB12, RAK RX <- PB13
Uart Console(PA3, PA2);               // USART2: console RX=PA3, TX=PA2

constexpr uint32_t RAK_BAUD = 115200; // Match the module's saved baud setting.
constexpr char P2P_SETTINGS[] = "AT+P2P=915000000:7:125:0:8:14";
constexpr char SEND_MESSAGE[] = "AT+PSEND=48656C6C6F2066726F6D2053544D3332";

// Visible in a debugger even if no console cable is connected.
volatile uint32_t rak_rx_bytes = 0;
volatile uint32_t rak_tx_confirmed_count = 0;
volatile uint32_t rak_timeout_count = 0;
volatile uint32_t rak_error_count = 0;
const char *volatile rak_last_command = "";
char rak_last_response[128] = {};
uint8_t rak_rx_raw[64] = {};
volatile uint32_t rak_rx_raw_count = 0;

static char line[128];
static size_t lineLength = 0;
static bool lineOverflow = false;
static bool configured = false;

// Override the generic board clock: HSI at 16 MHz, no external crystal.
// USB is disabled for this test; the console is a physical UART.
extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {};
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState = RCC_PLL_OFF;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

  RCC_ClkInitTypeDef clock = {};
  clock.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clock.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  clock.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clock.APB1CLKDivider = RCC_HCLK_DIV1;
  clock.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

static void discardInput(void) {
  while (Rak.available()) {
    Rak.read();
    ++rak_rx_bytes;
  }
  lineLength = 0;
  lineOverflow = false;
}

static void sendCommand(const char *command) {
  discardInput();
  rak_last_command = command;
  rak_last_response[0] = '\0';
  memset(rak_rx_raw, 0, sizeof(rak_rx_raw));
  rak_rx_raw_count = 0;
  Console.print("\r\n>> ");
  Console.println(command);
  Rak.print(command);
  Rak.print("\r\n");
  Rak.flush();
}

static bool waitForLine(const char *expected, uint32_t timeout) {
  const uint32_t start = millis();
  while (millis() - start < timeout) {
    if (!Rak.available()) {
      delay(1);
      continue;
    }
    const uint8_t byte = static_cast<uint8_t>(Rak.read());
    ++rak_rx_bytes;
    if (rak_rx_raw_count < sizeof(rak_rx_raw)) {
      rak_rx_raw[rak_rx_raw_count++] = byte;
    }
    Console.write(byte); // Show raw replies, including partial/garbled replies.
    if (byte == '\r') continue;
    if (byte != '\n') {
      if (lineLength < sizeof(line) - 1) line[lineLength++] = byte;
      else lineOverflow = true;
      continue;
    }
    line[lineLength] = '\0';
    const bool valid = !lineOverflow && lineLength != 0;
    lineLength = 0;
    lineOverflow = false;
    if (!valid) continue;
    strcpy(rak_last_response, line);
    if (strstr(line, "ERROR")) {
      ++rak_error_count;
      Console.println("\r\n[Module reported an error]");
      return false;
    }
    if (strcmp(line, expected) == 0) return true;
  }
  ++rak_timeout_count;
  Console.print("\r\n[Timeout waiting for ");
  Console.print(expected);
  Console.print("; captured bytes: ");
  Console.print(rak_rx_raw_count);
  Console.println("]");
  return false;
}

static bool commandOK(const char *command) {
  sendCommand(command);
  return waitForLine("OK", 2000);
}

static bool configureRadio(void) {
  if (!commandOK("AT")) return false;

  // A mode change can reboot the RAK. Allow reboot before checking again.
  sendCommand("AT+NWM=0");
  const uint32_t start = millis();
  while (millis() - start < 3000) {
    while (Rak.available()) {
      Console.write(static_cast<uint8_t>(Rak.read()));
      ++rak_rx_bytes;
    }
    delay(1);
  }
  if (!commandOK("AT")) return false;
  // UART replies are enabled; over-the-air reception stays disabled.
  if (!commandOK("AT+PRECV=0")) return false;
  return commandOK(P2P_SETTINGS);
}

void setup() {
  Console.begin(115200);
  Rak.begin(RAK_BAUD, SERIAL_8N1);
  delay(3000); // Allow module startup. No dependency on opening a monitor.
  Console.println("\r\nSTM32F413 / RAK3172 test (HSI 16 MHz)");
  Console.println("RAK UART5: RX=PB12, TX=PB13");
  Console.print("RAK baud: ");
  Console.println(RAK_BAUD);
}

void loop() {
  if (!configured) {
    configured = configureRadio();
    if (!configured) {
      Console.println("Setup failed; retrying in 5 seconds.");
      delay(5000);
      return;
    }
    Console.println("P2P configured. Sending Hello from STM32.");
  }

  if (commandOK(SEND_MESSAGE) && waitForLine("+EVT:TXP2P DONE", 5000)) {
    ++rak_tx_confirmed_count;
    Console.print("\r\n[TX confirmed] count=");
    Console.println(rak_tx_confirmed_count);
  } else {
    Console.println("TX unconfirmed; rechecking setup on next attempt.");
    configured = false;
  }
  delay(5000);
}
