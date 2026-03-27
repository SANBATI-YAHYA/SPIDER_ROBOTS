#include <WiFi.h>
#include <WiFiUdp.h>

const char *WIFI_SSID = "Helios";
const char *WIFI_PASSWORD = "a123456a";
const char *SERVER_IP = "10.221.49.10";
const uint16_t TELEMETRY_PORT = 5006;
const uint16_t MASTER_PORT = 5006;

const uint8_t MAGIC_CMD[2] = {0xCC, 0xDD};
const uint8_t MAGIC_SENSOR[2] = {0xEE, 0xFF};

// Commands
const uint8_t CMD_MOVE = 0x01;
const uint8_t CMD_STOP = 0x02;
const uint8_t CMD_SET_SPEED = 0x03;
const uint8_t CMD_FOLLOW = 0x04;
const uint8_t CMD_TURN = 0x05;
const uint8_t CMD_ESTOP = 0xFF;

WiFiUDP udp;

float readBEFloat(const uint8_t *buf) {
  uint32_t raw = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                 ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
  float f;
  memcpy(&f, &raw, sizeof(f));
  return f;
}

void sendTelemetry() {
  uint8_t buf[20];
  buf[0] = MAGIC_SENSOR[0];
  buf[1] = MAGIC_SENSOR[1];

  // Send a basic heartbeat packet so server records our IP
  float ts_stamp = millis() / 1000.0f;
  uint32_t raw_ts;
  memcpy(&raw_ts, &ts_stamp, sizeof(raw_ts));

  buf[2] = (raw_ts >> 24) & 0xFF;
  buf[3] = (raw_ts >> 16) & 0xFF;
  buf[4] = (raw_ts >> 8) & 0xFF;
  buf[5] = raw_ts & 0xFF;

  // battery_mv (uint16)
  buf[6] = 0;
  buf[7] = 0;
  // roll, pitch, yaw (float)
  memset(buf + 8, 0, 12);

  udp.beginPacket(SERVER_IP, TELEMETRY_PORT);
  udp.write(buf, sizeof(buf));
  udp.endPacket();
}

void receiveCommands() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0)
    return;

  uint8_t buf[256];
  int len = udp.read(buf, sizeof(buf));
  if (len < 5)
    return;

  if (buf[0] != MAGIC_CMD[0] || buf[1] != MAGIC_CMD[1])
    return;

  uint8_t cmdId = buf[2];
  uint16_t payloadLen = ((uint16_t)buf[3] << 8) | buf[4];
  const uint8_t *payload = buf + 5;

  switch (cmdId) {
  case CMD_MOVE: {
    if (payloadLen < 12)
      break;
    float vx = readBEFloat(payload);
    float vy = readBEFloat(payload + 4);
    float vz = readBEFloat(payload + 8);
    Serial.printf(">> [CMD] MOVE  vx=%.3f  vy=%.3f  vz=%.3f\n", vx, vy, vz);
    break;
  }

  case CMD_STOP:
    Serial.println(">> [CMD] STOP");
    break;

  case CMD_ESTOP:
    Serial.println(">> [CMD] EMERGENCY STOP");
    break;

  case CMD_TURN: {
    if (payloadLen < 4)
      break;
    float yawRate = readBEFloat(payload);
    Serial.printf(">> [CMD] TURN  yaw_rate=%.3f\n", yawRate);
    break;
  }

  case CMD_FOLLOW: {
    if (payloadLen < 12)
      break;
    float tx = readBEFloat(payload);
    float ty = readBEFloat(payload + 4);
    float depth = readBEFloat(payload + 8);
    Serial.printf(">> [CMD] FOLLOW  target=(%.3f, %.3f)  depth=%.3f\n", tx, ty,
                  depth);
    break;
  }

  case CMD_SET_SPEED: {
    if (payloadLen < 4)
      break;
    float speed = readBEFloat(payload);
    Serial.printf(">> [CMD] SET_SPEED  speed=%.3f\n", speed);
    break;
  }

  default:
    Serial.printf(">> [CMD] Unknown command: 0x%02X\n", cmdId);
    break;
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[NET] Connected!");

  udp.begin(MASTER_PORT);
}

void loop() {
  static uint32_t lastTelem = 0;
  if (millis() - lastTelem >= 1000) {
    sendTelemetry();
    lastTelem = millis();
  }

  receiveCommands();
  delay(5);
}
