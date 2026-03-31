#include <Arduino.h>
#include <rplidar_driver.h>

using namespace rp::standalone::rplidar;

// GPIO Pins Configuration
#define LIDAR_TX 16    // Yellow (TX) - ESP32 Serial2 RX
#define LIDAR_RX 17    // Green (RX) - ESP32 Serial2 TX
#define MOTOR_PIN 18   // Blue (Motor control)

// RPLidar commands
#define CMD_SYNC_BYTE     0xA5
#define CMD_START_SCAN    0x20
#define CMD_STOP          0x25
#define CMD_RESET         0x40
#define CMD_GET_INFO      0x50

// Response descriptor
#define RESP_SYNC1  0xA5
#define RESP_SYNC2  0x5A

// RPLIDAR object
RPlidarDriver *drv = NULL;
HardwareSerial lidarSerial(2); // Serial2 (GPIO16/17)

// RPLIDAR data structures
struct {
  uint8_t buffer[5];
  uint8_t index;
  bool header_found;
  uint16_t packet_len;
  uint8_t data_type;
  
  // Current measurement data
  uint8_t quality;
  uint16_t angle_raw;  // In 1/64 degree units (RPLIDAR A2 M8)
  uint16_t distance_raw;  // In mm
} parser;

uint32_t lastDataTime = 0;
bool hasReceivedData = false;
uint32_t byteCount = 0;  // Changed to uint32_t to avoid overflow
uint16_t pointCount = 0;
uint16_t packetCount = 0;
uint8_t hexLineBuffer[16];
uint8_t hexLineIndex = 0;
bool receivingData = false;

void sendLidarCommand(uint8_t cmd) {
  lidarSerial.write(CMD_SYNC_BYTE);
  lidarSerial.write(cmd);
  lidarSerial.flush();
  Serial.printf("\n[LIDAR CMD] Sent: 0x%02X 0x%02X\n", CMD_SYNC_BYTE, cmd);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n==== RPLIDAR A1 Initialization ====");
  Serial.printf("[PINS] LIDAR_TX=%d, LIDAR_RX=%d, MOTOR_PIN=%d\n", LIDAR_TX, LIDAR_RX, MOTOR_PIN);
  
  // Configure motor pin
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);
  Serial.printf("[SETUP] Motor pin GPIO%d disabled initially\n", MOTOR_PIN);
  
  // Initialize Serial2
  Serial.println("[SETUP] Initializing Serial2...");
  lidarSerial.setRxBufferSize(8192);
  lidarSerial.begin(115200, SERIAL_8N1, LIDAR_RX, LIDAR_TX);
  delay(200);
  Serial.printf("[SETUP] Serial2 initialized at 115200 baud (RX:GPIO%d, TX:GPIO%d)\n", LIDAR_RX, LIDAR_TX);
  
  // Create RPLIDAR driver
  Serial.println("[SETUP] Creating RPLIDAR driver...");
  drv = RPlidarDriver::CreateDriver();
  
  if (!drv) {
    Serial.println("[ERROR] Failed to allocate RPLIDAR driver!");
    while (1) { delay(1000); }
  }
  Serial.println("[SETUP] Driver created successfully");
  
  // Send reset command
  Serial.println("[SETUP] Sending LIDAR reset command...");
  delay(100);
  sendLidarCommand(CMD_RESET);
  delay(500);
  
  // Clear garbage
  while (lidarSerial.available()) {
    lidarSerial.read();
  }
  
  // Enable motor
  Serial.printf("[SETUP] Enabling motor on GPIO%d...\n", MOTOR_PIN);
  digitalWrite(MOTOR_PIN, HIGH);
  delay(1000);
  
  // Send start scan
  Serial.println("[SETUP] Sending START SCAN command...");
  sendLidarCommand(CMD_START_SCAN);
  delay(500);
  
  // Clear any startup garbage from LIDAR
  delay(500);
  while (lidarSerial.available()) {
    lidarSerial.read();
  }
  
  Serial.println("=====================================");
  Serial.println("RPLIDAR A2 M8 - Data Parsing Mode");
  Serial.println("Waiting for scan data...\n");
  
  lastDataTime = millis();
  parser.header_found = false;
  parser.index = 0;
}

void loop() {
  uint32_t now = millis();
  static uint32_t lastByteTime = 0;
  
  if (lidarSerial.available()) {
    uint8_t byte = lidarSerial.read();
    byteCount++;
    
    if (!hasReceivedData) {
      hasReceivedData = true;
      Serial.println("\n[SUCCESS] Data stream detected - sending RAW BINARY to Python reader");
    }
    
    // Send raw binary byte directly to Serial (for Python reader to parse)
    Serial.write(byte);
    
    lastByteTime = now;
    receivingData = true;
  }
  
  // Check for continuous data (no gaps > 100ms)
  if (receivingData && (now - lastByteTime) > 100) {
    if (byteCount > 100) {
      Serial.printf("\n\n[OK] Data is CONTINUOUS! Received %u bytes total\n", byteCount);
      Serial.println("[NEXT] Send 'p' to enable packet parsing in Serial Monitor");
      receivingData = false;
      delay(2000);
    }
  }
  
  // No data check
  if (!receivingData && !hasReceivedData && (now - lastDataTime) > 3000) {
    static uint32_t lastWarning = 0;
    if ((now - lastWarning) > 3000) {
      Serial.println("\n[WARNING] No data from A2 M8!");
      Serial.printf("Motor GPIO%d: %s\n", MOTOR_PIN, digitalRead(MOTOR_PIN) ? "SPINNING" : "STOPPED");
      Serial.printf("Check RX (GPIO%d) <- TX, TX (GPIO%d) -> RX\n", LIDAR_RX, LIDAR_TX);
      lastWarning = now;
    }
  }
  
  delay(1);
}
