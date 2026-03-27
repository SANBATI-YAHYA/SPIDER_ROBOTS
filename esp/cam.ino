#include "driver/rtc_io.h"
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// USER CONFIG
const char *WIFI_SSID = "Helios";
const char *WIFI_PASSWORD = "a123456a";
const char *SERVER_IP = "10.221.49.10";
const uint16_t VIDEO_PORT = 5005;
const uint16_t CAM_PORT = 5007;

const uint32_t FRAME_INTERVAL_MS = 66;
bool ENABLE_STREAM =
    true; // Set to false to stop continuous streaming and save bandwidth

// Protocol constants
const uint8_t MAGIC_VIDEO[2] = {0xAA, 0xBB};
const uint8_t MAGIC_CMD[2] = {0xCC, 0xDD};
const uint8_t MAGIC_HANDSHAKE[2] = {0x11, 0x22};
const uint8_t MAGIC_ACK[2] = {0x33, 0x44};

// Camera framesize lookup
static const framesize_t FRAMESIZE_MAP[] = {
    FRAMESIZE_96X96,   // 0
    FRAMESIZE_QQVGA,   // 1
    FRAMESIZE_QCIF,    // 2
    FRAMESIZE_HQVGA,   // 3
    FRAMESIZE_240X240, // 4
    FRAMESIZE_QVGA,    // 5
    FRAMESIZE_CIF,     // 6
    FRAMESIZE_HVGA,    // 7
    FRAMESIZE_VGA,     // 8  (640x480)
    FRAMESIZE_SVGA,    // 9
    FRAMESIZE_XGA,     // 10
    FRAMESIZE_HD,      // 11
    FRAMESIZE_SXGA,    // 12
    FRAMESIZE_UXGA,    // 13
};
static const uint8_t FRAMESIZE_MAP_LEN =
    sizeof(FRAMESIZE_MAP) / sizeof(FRAMESIZE_MAP[0]);

static uint8_t udpBuf[CHUNK_SIZE + 8];

volatile uint32_t g_frameIntervalMs = FRAME_INTERVAL_MS;

const uint8_t CMD_SCAN_REFLECTIONS = 0x06;

const size_t CHUNK_SIZE = 1400;

// Flash pin
#define FLASH_GPIO_NUM 4

// Camera pins
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

WiFiUDP udp;
uint16_t frameSeq = 0;

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA; // Lowered from VGA to save PSRAM
    config.jpeg_quality = 12; // 10-63 lower number means higher quality
    config.fb_count = 1;      // Lowered to 1 to minimize memory footprint
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size =
        FRAMESIZE_QQVGA; // Use smallest resolution for no-PSRAM fallback
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);

  Serial.println("[CAM] Initialised OK");
  return true;
}

void sendFrame(const uint8_t *jpeg, size_t len, uint8_t flash_state = 0) {
  size_t totalChunks = (len + CHUNK_SIZE - 1) / CHUNK_SIZE;
  if (totalChunks > 255)
    return;

  uint8_t *buf = udpBuf;

  for (uint8_t chunkId = 0; chunkId < (uint8_t)totalChunks; chunkId++) {
    size_t offset = chunkId * CHUNK_SIZE;
    size_t chunkLen = min(CHUNK_SIZE, len - offset);

    buf[0] = MAGIC_VIDEO[0];
    buf[1] = MAGIC_VIDEO[1];
    buf[2] = (frameSeq >> 8) & 0xFF;
    buf[3] = frameSeq & 0xFF;
    buf[4] = chunkId;
    buf[5] = (uint8_t)totalChunks;
    buf[6] = flash_state;
    buf[7] = 0;

    memcpy(buf + 8, jpeg + offset, chunkLen);

    udp.beginPacket(SERVER_IP, VIDEO_PORT);
    udp.write(buf, 8 + chunkLen);
    udp.endPacket();
  }
  frameSeq++;
}

void applyConfig(uint8_t resolutionId, uint8_t fps, uint8_t quality) {
  if (resolutionId < FRAMESIZE_MAP_LEN) {
    sensor_t *s = esp_camera_sensor_get();
    if (s)
      s->set_framesize(s, FRAMESIZE_MAP[resolutionId]);
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s)
    s->set_quality(s, quality);

  if (fps > 0 && fps <= 30) {
    g_frameIntervalMs = 1000UL / fps;
  }

  uint8_t ack[5];
  ack[0] = MAGIC_ACK[0];
  ack[1] = MAGIC_ACK[1];
  ack[2] = resolutionId;
  ack[3] = fps;
  ack[4] = quality;
  udp.beginPacket(SERVER_IP, VIDEO_PORT);
  udp.write(ack, sizeof(ack));
  udp.endPacket();
}

void receiveCommands() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0)
    return;

  uint8_t *buf = udpBuf;
  int len = udp.read(buf, sizeof(buf));
  if (len < 2)
    return;

  if (buf[0] == MAGIC_HANDSHAKE[0] && buf[1] == MAGIC_HANDSHAKE[1]) {
    if (len >= 5)
      applyConfig(buf[2], buf[3], buf[4]);
    return;
  }

  if (buf[0] != MAGIC_CMD[0] || buf[1] != MAGIC_CMD[1])
    return;
  if (len < 5)
    return;

  uint8_t cmdId = buf[2];
  uint16_t payloadLen = ((uint16_t)buf[3] << 8) | buf[4];

  if (cmdId == CMD_SCAN_REFLECTIONS) {
    Serial.println("[CMD] SCAN_REFLECTIONS");

    digitalWrite(FLASH_GPIO_NUM, LOW);
    camera_fb_t *fb_off = esp_camera_fb_get();
    if (fb_off) {
      sendFrame(fb_off->buf, fb_off->len, 0);
      esp_camera_fb_return(fb_off);
    }

    digitalWrite(FLASH_GPIO_NUM, HIGH);
    delay(300); // Wait long enough for flash to fire and sensor to capture it
    camera_fb_t *fb_on = esp_camera_fb_get();
    if (fb_on) {
      sendFrame(fb_on->buf, fb_on->len, 1);
      esp_camera_fb_return(fb_on);
    }
    digitalWrite(FLASH_GPIO_NUM, LOW);
  }
}

void setup() {
  Serial.begin(115200);

  // Explicitly un-hold the RTC IO which can sometimes lock the flash line
  rtc_gpio_hold_dis(GPIO_NUM_4);
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  if (!initCamera()) {
    while (true)
      delay(1000);
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  udp.begin(CAM_PORT);
}

void loop() {
  static uint32_t lastFrameMs = 0;
  uint32_t now = millis();

  if (ENABLE_STREAM && (now - lastFrameMs >= g_frameIntervalMs)) {
    lastFrameMs = now;
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      sendFrame(fb->buf, fb->len, 0);
      esp_camera_fb_return(fb);
    }
  }

  receiveCommands();
  yield();
}
