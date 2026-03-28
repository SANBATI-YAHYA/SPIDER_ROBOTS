// ═══════════════════════════════════════════════════════════════════════════════
//  ESP32-CAM COLOR DETECTION (Simple Version)
//  This code should run on ESP32-CAM module (separate from master robot controller)
//  ═══════════════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebServer.h>

const char *SSID     = "EAGLES";
const char *PASSWORD = "EAGLES06";

WebServer server(80);

// ─────────────────────────────────────────────────────────────────────────────
//  SIMPLE COLOR DETECTION (without full camera setup)
//  Replace with actual color detection library if you have OpenCV or ArduCam
// ─────────────────────────────────────────────────────────────────────────────

String detectedColor = "NONE";
unsigned long lastDetectionTime = 0;

void performColorDetection() {
  // ⚠️  This is a PLACEHOLDER for actual color detection
  // In real implementation, capture frame and analyze RGB values
  // For now, we'll simulate detection with timing
  
  // Example: Detect color every 100ms
  if (millis() - lastDetectionTime > 100) {
    lastDetectionTime = millis();
    
    // TODO: Add actual color detection logic here
    // Example pseudocode:
    // int red = getRedChannel();
    // int green = getGreenChannel();
    // int blue = getBlueChannel();
    // if (red > 150 && green < 100 && blue < 100) {
    //   detectedColor = "RED";
    // } else if (green > 150 && red < 100 && blue < 100) {
    //   detectedColor = "GREEN";
    // }
    
    // For testing: Cycle colors
    static int colorIndex = 0;
    switch(colorIndex) {
      case 0: detectedColor = "RED"; break;
      case 1: detectedColor = "GREEN"; break;
      case 2: detectedColor = "BLUE"; break;
      case 3: detectedColor = "NONE"; break;
    }
    colorIndex = (colorIndex + 1) % 4;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  WEB SERVER - Responds with detected color
// ─────────────────────────────────────────────────────────────────────────────

void handleDetect() {
  // Master robot calls this endpoint: GET /detect
  // We respond with detected color
  server.send(200, "text/plain", detectedColor);
  Serial.println("Master asked for color → " + detectedColor);
}

void handleRoot() {
  server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head><title>ESP32-CAM Color Detector</title></head>
    <body>
      <h1>🎥 ESP32-CAM Color Detection</h1>
      <p>Current Detection: <strong id="color">NONE</strong></p>
      <p>IP: <script>document.write(window.location.hostname)</script></p>
      <script>
        setInterval(() => {
          fetch('/detect')
            .then(r => r.text())
            .then(color => document.getElementById('color').textContent = color);
        }, 500);
      </script>
    </body>
    </html>
  )rawliteral");
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32-CAM COLOR DETECTOR BOOT ===");

  // Connect to WiFi
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Setup web routes
    server.on("/", handleRoot);
    server.on("/detect", handleDetect);
    server.begin();
    Serial.println("✓ Web server started on port 80");
    Serial.println("Master robot should call: http://" + WiFi.localIP().toString() + "/detect");
  } else {
    Serial.println("\n✗ WiFi FAILED");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();
  performColorDetection();
  delay(10);
}

// ═════════════════════════════════════════════════════════════════════════════
//  HOW TO USE:
//  1. Upload this code to ESP32-CAM board
//  2. It will connect to "EAGLES" WiFi network
//  3. Note its IP address from Serial Monitor (e.g., 192.168.1.50)
//  4. Update CAMERA_IP in master.ino to match this IP
//  5. Master robot will now poll: GET /detect
//  6. Replace performColorDetection() with real color detection algorithm
// ═════════════════════════════════════════════════════════════════════════════
