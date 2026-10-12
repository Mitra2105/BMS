
#include <Arduino.h>
#include <WiFi.h>

// ================= WIFI RECEIVER =================
const char* WIFI_SSID = "powerten-server";
const char* WIFI_PASSWORD = "12345678";

IPAddress receiverIP(192, 168, 4, 1);
const uint16_t receiverPort = 5000;
const char* DEVICE_NAME = "BMS";

// ================= RS485 CONFIGURATION =================
#define RS485_TX 15
#define RS485_RX 16
#define RS485_DE 21
#define BMS_BAUD 9600

HardwareSerial BMS(1);
WiFiClient tcpClient;

const char BMS_REQUEST[] = "~201246610000FDAA\r";

String responseFrame = "";
unsigned long lastPoll = 0;
const unsigned long POLL_INTERVAL = 3000;

// ================= WIFI =================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Powerten");
  Serial.print(WIFI_SSID);
  Serial.print(WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected");
  Serial.print("Device name: ");
  Serial.println(DEVICE_NAME);
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// ================= TCP RECEIVER =================
bool connectReceiver() {
  if (tcpClient.connected()) {
    return true;
  }

  tcpClient.stop();

  Serial.print("Connecting to receiver ");
  Serial.print(receiverIP);
  Serial.print(":");
  Serial.println(receiverPort);

  if (tcpClient.connect(receiverIP, receiverPort)) {
    Serial.println("TCP receiver connected");
    return true;
  }

  Serial.println("TCP connection failed");
  return false;
}

// ================= HEX DECODING =================
uint32_t hexValue(const String& value) {
  return (uint32_t)strtoul(value.c_str(), nullptr, 16);
}

uint16_t readU16(const String& data, int& pos) {
  uint16_t value =
      (uint16_t)hexValue(data.substring(pos, pos + 4));
  pos += 4;
  return value;
}

float decodeTemperature(uint16_t raw) {
  return ((float)raw - 2731.0f) / 10.0f;
}

// ================= JSON TRANSMISSION =================
void sendJSON(const char* json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected");
    return;
  }

  if (!connectReceiver()) {
    Serial.println("Could not send data");
    return;
  }

  // One JSON object per line
  tcpClient.println(json);

  Serial.println("JSON sent to receiver:");
  Serial.println(json);
}

// ================= DECODE BMS FRAME =================
void decodeAndSend(String frame) {
  frame.trim();

  if (frame.length() < 20 || frame[0] != '~') {
    Serial.println("Invalid BMS frame");
    return;
  }

  // Expected response header:
  // ~ VER ADR CID1 RTN LCHKSUM INFO
  if (frame.substring(1, 3) != "20" ||
      frame.substring(5, 7) != "46") {
    Serial.println("Unexpected response header");
    Serial.println(frame);
    return;
  }

  // In this protocol, the last 3 hex digits of the
  // four-character length field represent INFO length.
  int infoLength = (int)hexValue(frame.substring(10, 13));

  if (infoLength != 98 ||
      frame.length() < 13 + infoLength + 4) {
    Serial.println("Unexpected telemetry length");
    Serial.println(frame);
    return;
  }

  String data = frame.substring(13, 13 + infoLength);

  for (int i = 0; i < data.length(); i++) {
    if (!isxdigit(data[i])) {
      Serial.println("Invalid hexadecimal data");
      return;
    }
  }

  int p = 0;

  float voltage_V = readU16(data, p) / 1000.0f;

  int16_t currentRaw = (int16_t)readU16(data, p);
  float current_A = currentRaw / 1000.0f;

  int soc = (int)hexValue(data.substring(p, p + 2));
  p += 2;

  uint16_t averageCycles = readU16(data, p);
  uint16_t maximumCycles = readU16(data, p);

  int averageSOH = (int)hexValue(data.substring(p, p + 2));
  p += 2;

  int minimumSOH = (int)hexValue(data.substring(p, p + 2));
  p += 2;

  float highestCellV = readU16(data, p) / 1000.0f;
  uint16_t highestCellLocation = readU16(data, p);

  float lowestCellV = readU16(data, p) / 1000.0f;
  uint16_t lowestCellLocation = readU16(data, p);

  float averageCellTemp = decodeTemperature(readU16(data, p));
  float maximumCellTemp = decodeTemperature(readU16(data, p));
  uint16_t maximumCellTempLocation = readU16(data, p);

  float minimumCellTemp = decodeTemperature(readU16(data, p));
  uint16_t minimumCellTempLocation = readU16(data, p);

  float mosfetAverageTemp = decodeTemperature(readU16(data, p));
  float mosfetMaximumTemp = decodeTemperature(readU16(data, p));
  uint16_t mosfetMaximumLocation = readU16(data, p);

  float mosfetMinimumTemp = decodeTemperature(readU16(data, p));
  uint16_t mosfetMinimumLocation = readU16(data, p);

  float bmsAverageTemp = decodeTemperature(readU16(data, p));
  float bmsMaximumTemp = decodeTemperature(readU16(data, p));
  uint16_t bmsMaximumLocation = readU16(data, p);

  float bmsMinimumTemp = decodeTemperature(readU16(data, p));
  uint16_t bmsMinimumLocation = readU16(data, p);

  // ================= SERIAL OUTPUT =================
  Serial.println("\n========== BMS TELEMETRY ==========");
  Serial.printf("Device: BMS\n");
  Serial.printf("Pack voltage: %.3f V\n", voltage_V);
  Serial.printf("Current: %.3f A\n", current_A);
  Serial.printf("SOC: %d %%\n", soc);
  Serial.printf("Average cycles: %u\n", averageCycles);
  Serial.printf("Maximum cycles: %u\n", maximumCycles);
  Serial.printf("Average SOH: %d %%\n", averageSOH);
  Serial.printf("Minimum SOH: %d %%\n", minimumSOH);

  Serial.printf("Highest cell: %.3f V (location %u)\n",
                highestCellV, highestCellLocation);
  Serial.printf("Lowest cell: %.3f V (location %u)\n",
                lowestCellV, lowestCellLocation);

  Serial.printf("Average cell temperature: %.1f C\n",
                averageCellTemp);
  Serial.printf("Maximum cell temperature: %.1f C (location %u)\n",
                maximumCellTemp, maximumCellTempLocation);
  Serial.printf("Minimum cell temperature: %.1f C (location %u)\n",
                minimumCellTemp, minimumCellTempLocation);

  Serial.printf("MOSFET average temperature: %.1f C\n",
                mosfetAverageTemp);
  Serial.printf("MOSFET maximum temperature: %.1f C (location %u)\n",
                mosfetMaximumTemp, mosfetMaximumLocation);
  Serial.printf("MOSFET minimum temperature: %.1f C (location %u)\n",
                mosfetMinimumTemp, mosfetMinimumLocation);

  Serial.printf("BMS average temperature: %.1f C\n", bmsAverageTemp);
  Serial.printf("BMS maximum temperature: %.1f C (location %u)\n",
                bmsMaximumTemp, bmsMaximumLocation);
  Serial.printf("BMS minimum temperature: %.1f C (location %u)\n",
                bmsMinimumTemp, bmsMinimumLocation);

  Serial.println("===================================");

  // ================= CREATE JSON =================
  char json[1400];

  snprintf(
    json, sizeof(json),
    "{\"device\":\"BMS\","
    "\"voltage_V\":%.3f,"
    "\"current_A\":%.3f,"
    "\"soc_percent\":%d,"
    "\"average_cycles\":%u,"
    "\"maximum_cycles\":%u,"
    "\"average_soh_percent\":%d,"
    "\"minimum_soh_percent\":%d,"
    "\"highest_cell_voltage_V\":%.3f,"
    "\"highest_cell_location\":%u,"
    "\"lowest_cell_voltage_V\":%.3f,"
    "\"lowest_cell_location\":%u,"
    "\"average_cell_temperature_C\":%.1f,"
    "\"maximum_cell_temperature_C\":%.1f,"
    "\"maximum_cell_temperature_location\":%u,"
    "\"minimum_cell_temperature_C\":%.1f,"
    "\"minimum_cell_temperature_location\":%u,"
    "\"mosfet_average_temperature_C\":%.1f,"
    "\"mosfet_maximum_temperature_C\":%.1f,"
    "\"mosfet_maximum_temperature_location\":%u,"
    "\"mosfet_minimum_temperature_C\":%.1f,"
    "\"mosfet_minimum_temperature_location\":%u,"
    "\"bms_average_temperature_C\":%.1f,"
    "\"bms_maximum_temperature_C\":%.1f,"
    "\"bms_maximum_temperature_location\":%u,"
    "\"bms_minimum_temperature_C\":%.1f,"
    "\"bms_minimum_temperature_location\":%u}",
    voltage_V, current_A, soc,
    averageCycles, maximumCycles, averageSOH, minimumSOH,
    highestCellV, highestCellLocation,
    lowestCellV, lowestCellLocation,
    averageCellTemp, maximumCellTemp, maximumCellTempLocation,
    minimumCellTemp, minimumCellTempLocation,
    mosfetAverageTemp, mosfetMaximumTemp, mosfetMaximumLocation,
    mosfetMinimumTemp, mosfetMinimumLocation,
    bmsAverageTemp, bmsMaximumTemp, bmsMaximumLocation,
    bmsMinimumTemp, bmsMinimumLocation
  );

  sendJSON(json);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);

  BMS.begin(BMS_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

  Serial.println("\nStarting BMS device...");
  connectWiFi();
  connectReceiver();
}

// ================= MAIN LOOP =================
void loop() {
  // Request BMS telemetry every 3 seconds
  if (millis() - lastPoll >= POLL_INTERVAL) {
    lastPoll = millis();

    while (BMS.available()) {
      BMS.read();
    }

    digitalWrite(RS485_DE, HIGH);
    delay(2);
    BMS.print(BMS_REQUEST);
    BMS.flush();
    delay(2);
    digitalWrite(RS485_DE, LOW);
  }

  // Collect the BMS response frame
  while (BMS.available()) {
    char c = (char)BMS.read();

    if (c == '~') {
      responseFrame = "~";
    } else if (responseFrame.length() > 0) {
      if (c == '\r' || c == '\n') {
        if (responseFrame.length() > 1) {
          Serial.print("Raw BMS frame: ");
          Serial.println(responseFrame);
          decodeAndSend(responseFrame);
        }

        responseFrame = "";
      } else if (responseFrame.length() < 300) {
        responseFrame += c;
      } else {
        Serial.println("Frame too long; discarded");
        responseFrame = "";
      }
    }
  }

  // Recover Wi-Fi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
}
