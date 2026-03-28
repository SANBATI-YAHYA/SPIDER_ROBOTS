#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

#ifdef ESP32
#include <WiFi.h>
#include <WebServer.h>
WebServer server(80);
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
ESP8266WebServer server(80);
#endif

// ── WiFi credentials ─────────────────────────────────────────────────────────
const char *SSID     = "EAGLES";
const char *PASSWORD = "EAGLES06";

// ── PCA9685 ──────────────────────────────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// ── Pulse limits ─────────────────────────────────────────────────────────────
#define SERVO_MIN 102
#define SERVO_MAX 512

// ── Channel assignments ───────────────────────────────────────────────────────
#define CH_FLT 0
#define CH_FLB 2
#define CH_FRT 4
#define CH_FRB 6
#define CH_BRT 8
#define CH_BRB 10
#define CH_BLT 12
#define CH_BLB 14

// ── Stand angles ──────────────────────────────────────────────────────────────
#define S_FLT 100
#define S_FLB 121
#define S_FRT 115
#define S_FRB 107
#define S_BRT 165   // real stand
#define S_BRB 118
#define S_BLT 130
#define S_BLB 127

// ── G_BRT: gait-safe BRT ──────────────────────────────────────────────────────
// S_BRT=165. 165+25=190 → overflow → pin 10 goes to max (the bug).
// All gait functions use G_BRT=145 so ±STEP always stays in [120..170].
#define G_BRT 145

// ── Gait parameters ──────────────────────────────────────────────────────────
#define STEP        25   // hip swing for walking
#define LIFT        25   // knee lift for walking
#define LIFT_BACK   20   // smaller lift for backward (safer for BRB)
#define TURN_STEP   35   // hip swing for BIG turns (more angle = more arc)
#define PIVOT_STEP  40   // hip swing for in-place pivot (bigger = faster spin)
#define PHASE_DELAY 300  // ms between phases

// ── Walk mode ─────────────────────────────────────────────────────────────────
String walkMode = "stop";

// =============================================================================
//  LOW-LEVEL
// =============================================================================

uint16_t angleToPulse(int angle) {
  return map(constrain(angle, 0, 180), 0, 180, SERVO_MIN, SERVO_MAX);
}

void setServo(uint8_t ch, int angle) {
  angle = constrain(angle, 0, 180);
  pwm.setPWM(ch, 0, angleToPulse(angle));
  Serial.printf("  ch%-2d = %d deg\n", ch, angle);
}

// Reset only hips to stand (used inside pivot — avoids full standUp drift)
void resetHips() {
  setServo(CH_FLT, S_FLT);
  setServo(CH_FRT, S_FRT);
  setServo(CH_BRT, S_BRT);
  setServo(CH_BLT, S_BLT);
}

// =============================================================================
//  STAND
// =============================================================================

void standUp() {
  Serial.println(F("=== STAND ==="));
  setServo(CH_FLT, S_FLT);
  setServo(CH_FLB, S_FLB);
  setServo(CH_FRT, S_FRT);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BRT, S_BRT);   // 165 real stand
  setServo(CH_BRB, S_BRB);
  setServo(CH_BLT, S_BLT);
  setServo(CH_BLB, S_BLB);
  delay(PHASE_DELAY);
}

// =============================================================================
//  MOVE LEFT  (= forward for this robot)
//  Diagonal pairs: FL+BR | FR+BL
// =============================================================================

void phaseA() {
  setServo(CH_FLT, S_FLT + STEP);
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRT, G_BRT - STEP);      // 145-25=120 safe
  setServo(CH_BRB, S_BRB + LIFT);
  setServo(CH_FRT, S_FRT - STEP);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLT, S_BLT + STEP);
  setServo(CH_BLB, S_BLB);
  delay(PHASE_DELAY);
}

void phaseB() {
  setServo(CH_FRT, S_FRT + STEP);
  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLT, S_BLT - STEP);
  setServo(CH_BLB, S_BLB + LIFT);
  setServo(CH_FLT, S_FLT - STEP);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRT, G_BRT + STEP);      // 145+25=170 safe
  setServo(CH_BRB, S_BRB);
  delay(PHASE_DELAY);
}

void moveLeft() {
  Serial.println(F("=== MOVE LEFT ==="));
  phaseA();
  phaseB();
}

// =============================================================================
//  MOVE RIGHT  (mirror of left)
// =============================================================================

void phaseA_right() {
  setServo(CH_FLT, S_FLT - STEP);
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRT, G_BRT + STEP);      // 145+25=170 safe
  setServo(CH_BRB, S_BRB + LIFT);
  setServo(CH_FRT, S_FRT + STEP);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLT, S_BLT - STEP);
  setServo(CH_BLB, S_BLB);
  delay(PHASE_DELAY);
}

void phaseB_right() {
  setServo(CH_FRT, S_FRT - STEP);
  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLT, S_BLT + STEP);
  setServo(CH_BLB, S_BLB + LIFT);
  setServo(CH_FLT, S_FLT + STEP);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRT, G_BRT - STEP);      // 145-25=120 safe
  setServo(CH_BRB, S_BRB);
  delay(PHASE_DELAY);
}

void moveRight() {
  Serial.println(F("=== MOVE RIGHT ==="));
  phaseA_right();
  phaseB_right();
}

// =============================================================================
//  MOVE BACKWARD
// =============================================================================

void phaseA_back() {
  // Reset knees first to prevent BRB lock
  setServo(CH_FLB, S_FLB); setServo(CH_FRB, S_FRB);
  setServo(CH_BRB, S_BRB); setServo(CH_BLB, S_BLB);
  delay(80);
  setServo(CH_FLT, S_FLT - STEP);
  setServo(CH_FLB, S_FLB - LIFT_BACK);
  setServo(CH_BRT, G_BRT + STEP);      // 145+25=170 safe
  setServo(CH_BRB, S_BRB + LIFT_BACK);
  setServo(CH_FRT, S_FRT + STEP);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLT, S_BLT - STEP);
  setServo(CH_BLB, S_BLB);
  delay(PHASE_DELAY);
}

void phaseB_back() {
  setServo(CH_FLB, S_FLB); setServo(CH_FRB, S_FRB);
  setServo(CH_BRB, S_BRB); setServo(CH_BLB, S_BLB);
  delay(80);
  setServo(CH_FRT, S_FRT - STEP);
  setServo(CH_FRB, S_FRB - LIFT_BACK);
  setServo(CH_BLT, S_BLT + STEP);
  setServo(CH_BLB, S_BLB + LIFT_BACK);
  setServo(CH_FLT, S_FLT + STEP);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRT, G_BRT - STEP);      // 145-25=120 safe
  setServo(CH_BRB, S_BRB);
  delay(PHASE_DELAY);
}

void moveBackward() {
  Serial.println(F("=== BACKWARD ==="));
  phaseA_back();
  phaseB_back();
}

// =============================================================================
//  BIG TURN RIGHT — arcs while turning (renamed from turnRight)
//  Good for wide turns. Calls standUp() between repeats → causes arc movement.
// =============================================================================

void bigTurnRight() {
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRB, S_BRB + LIFT);
  delay(150);
  setServo(CH_FLT, S_FLT + TURN_STEP);
  setServo(CH_BRT, G_BRT + TURN_STEP);   // 145+35=180 ok
  delay(PHASE_DELAY);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRB, S_BRB);
  delay(150);

  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLB, S_BLB + LIFT);
  delay(150);
  setServo(CH_FRT, S_FRT - TURN_STEP);
  setServo(CH_BLT, S_BLT - TURN_STEP);
  delay(PHASE_DELAY);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLB, S_BLB);
  delay(150);

  standUp();
}

// =============================================================================
//  BIG TURN LEFT — arcs while turning (renamed from turnLeft)
// =============================================================================

void bigTurnLeft() {
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRB, S_BRB + LIFT);
  delay(150);
  setServo(CH_FLT, S_FLT - TURN_STEP); 
  setServo(CH_BRT, G_BRT - TURN_STEP); 
  delay(PHASE_DELAY);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRB, S_BRB);
  delay(150);

  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLB, S_BLB + LIFT);
  delay(150);
  setServo(CH_FRT, S_FRT + TURN_STEP);
  setServo(CH_BLT, S_BLT + TURN_STEP);
  delay(PHASE_DELAY);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLB, S_BLB);
  delay(150);

  standUp();
}

// =============================================================================
//  PIVOT RIGHT — true in-place spin
//
//  Key difference vs BIG TURN:
//  - NO standUp() between diagonal phases → no drift/translation
//  - resetHips() only at end of full cycle
//  - Both diagonal pairs swing in OPPOSITE directions
//    so the net linear force = 0, only rotational force remains
//
//  If robot spins LEFT instead of RIGHT → swap pivotRight and pivotLeft calls
//  in the web handlers, or flip the hip directions below.
// =============================================================================

void pivotRight() {
  // ── Diagonal A: FL + BR ──────────────────────────────────────────────────
  // Lift
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRB, S_BRB + LIFT);
  delay(150);
  // Swing: FL backward, BR forward → body rotates CW (right)
  setServo(CH_FLT, S_FLT - PIVOT_STEP);    // FL hip back
  setServo(CH_BRT, G_BRT - PIVOT_STEP);    // BR hip forward (145-40=105 safe)
  delay(PHASE_DELAY);
  // Place down — do NOT call standUp() here (causes drift)
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRB, S_BRB);
  delay(150);

  // ── Diagonal B: FR + BL ──────────────────────────────────────────────────
  // Lift
  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLB, S_BLB + LIFT);
  delay(150);
  // Swing: FR forward, BL backward → continues CW rotation
  setServo(CH_FRT, S_FRT + PIVOT_STEP);    // FR hip forward
  setServo(CH_BLT, S_BLT + PIVOT_STEP);   // BL hip backward
  delay(PHASE_DELAY);
  // Place down
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLB, S_BLB);
  delay(150);

  // ── Reset hips only (not full standUp) ───────────────────────────────────
  resetHips();
  delay(100);
}

// =============================================================================
//  PIVOT LEFT — mirror of pivotRight
// =============================================================================

void pivotLeft() {
  // ── Diagonal A: FL + BR ──────────────────────────────────────────────────
  setServo(CH_FLB, S_FLB - LIFT);
  setServo(CH_BRB, S_BRB + LIFT);
  delay(150);
  // Swing: FL forward, BR backward → body rotates CCW (left)
  setServo(CH_FLT, S_FLT + PIVOT_STEP);    // FL hip forward
  setServo(CH_BRT, G_BRT + PIVOT_STEP);    // BR hip backward (145+40=185→180 clamped, ok)
  delay(PHASE_DELAY);
  setServo(CH_FLB, S_FLB);
  setServo(CH_BRB, S_BRB);
  delay(150);

  // ── Diagonal B: FR + BL ──────────────────────────────────────────────────
  setServo(CH_FRB, S_FRB - LIFT);
  setServo(CH_BLB, S_BLB + LIFT);
  delay(150);
  // Swing: FR backward, BL forward → continues CCW rotation
  setServo(CH_FRT, S_FRT - PIVOT_STEP);    // FR hip backward
  setServo(CH_BLT, S_BLT - PIVOT_STEP);   // BL hip forward
  delay(PHASE_DELAY);
  setServo(CH_FRB, S_FRB);
  setServo(CH_BLB, S_BLB);
  delay(150);

  // ── Reset hips only ───────────────────────────────────────────────────────
  resetHips();
  delay(100);
}

// =============================================================================
//  WEB UI
// =============================================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Eagles Controller</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      min-height: 100vh;
      display: flex; flex-direction: column;
      align-items: center; justify-content: center;
      background: #0d0d0d; color: #e8e8e8;
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    }
    .container {
      background: #1a1a1a;
      padding: 2rem;
      border-radius: 15px;
      box-shadow: 0 0 20px rgba(0, 144, 54, 0.2);
      border: 1px solid #009036;
      text-align: center;
    }
    h1 { 
      font-size: 1.8rem; 
      letter-spacing: 0.1em; 
      margin-bottom: 1.5rem; 
      color: #009036; 
      display: flex; align-items: center; justify-content: center; gap: 10px;
    }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 12px;
      width: 300px;
    }
    .label {
      grid-column: span 3;
      font-size: 0.75rem;
      color: #009036;
      text-align: center;
      margin: 1rem 0 0.5rem 0;
      text-transform: uppercase;
      font-weight: bold;
      opacity: 0.7;
    }
    button {
      padding: 1.2rem 0.5rem; 
      font-size: 0.9rem; 
      font-weight: 600;
      border: 2px solid #009036; 
      border-radius: 8px;
      background: transparent; 
      color: #009036;
      cursor: pointer; 
      transition: all 0.2s ease;
    }
    button:active {
      transform: scale(0.95);
      background: #009036;
      color: #0d0d0d;
    }
    button.special {
      background: rgba(0, 144, 54, 0.1);
    }
    button.stop {
      border-color: #ff4444;
      color: #ff4444;
    }
    button.stop:active {
      background: #ff4444;
      color: white;
    }
    button.wide { grid-column: span 3; }
    #status {
      margin-top: 1.5rem; 
      font-size: 0.9rem;
      color: #009036; 
      font-family: monospace;
      padding: 8px;
      border-radius: 4px;
      background: rgba(0, 144, 54, 0.05);
    }
    .eagle-icon { font-size: 2.5rem; margin-bottom: 0.5rem; }
  </style>
</head>
<body>
  <div class="container">
    <div class="eagle-icon">🦅</div>
    <h1>EAGLES CTRL</h1>
    
    <div class="grid">
      <!-- PIVOT TURNS -->
      <div class="label">── Pivot ──</div>
      <button class="special" onclick="cmd('pivotleft')">↺ L</button>
      <button class="stop" onclick="cmd('stop')">STOP</button>
      <button class="special" onclick="cmd('pivotright')">↻ R</button>

      <!-- MOVEMENT -->
      <div class="label">── Movement ──</div>
      <button onclick="cmd('bigleft')">◀ BIG</button>
      <button class="special" onclick="cmd('forward')">▲ FWD</button>
      <button onclick="cmd('bigright')">BIG ▶</button>

      <button onclick="cmd('left')">◀ L</button>
      <button onclick="cmd('stand')">STAND</button>
      <button onclick="cmd('right')">R ▶</button>

      <button class="wide special" onclick="cmd('backward')">▼ BACKWARD</button>
    </div>

    <div id="status">System Ready</div>
  </div>

  <script>
    async function cmd(action) {
      const status = document.getElementById('status');
      status.textContent = 'Executing: ' + action;
      try {
        const r = await fetch('/' + action);
        const text = await r.text();
        status.textContent = 'Status: ' + text;
      } catch(e) {
        status.textContent = 'Error: ' + e;
        status.style.color = '#ff4444';
      }
    }
  </script>
</body>
</html>
)rawliteral";

// =============================================================================
//  WEB SERVER HANDLERS

void handleRoot()       { server.send(200, "text/html",  INDEX_HTML); }
void handleForward()    { walkMode = "forward";  server.send(200, "text/plain", "moving forward"); }
void handleLeft()       { walkMode = "left";     server.send(200, "text/plain", "moving left"); }
void handleRight()      { walkMode = "right";    server.send(200, "text/plain", "moving right"); }
void handleBackward()   { walkMode = "backward"; server.send(200, "text/plain", "moving backward"); }
void handleStop()       { walkMode = "stop"; standUp(); server.send(200, "text/plain", "stopped"); }
void handleStand()      { walkMode = "stop"; standUp(); server.send(200, "text/plain", "standing"); }
void handleBigLeft()    { walkMode = "bigleft";    server.send(200, "text/plain", "big turn left"); }
void handleBigRight()   { walkMode = "bigright";   server.send(200, "text/plain", "big turn right"); }
void handlePivotRight() { walkMode = "pivotright"; server.send(200, "text/plain", "pivot right"); }
void handlePivotLeft()  { walkMode = "pivotleft";  server.send(200, "text/plain", "pivot left"); }
void handleNotFound()   { server.send(404, "text/plain", "not found"); }

// =============================================================================
//  SETUP & LOOP
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== Spider Robot Boot ==="));

  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);
  delay(100);

  standUp();
  delay(1000);

  WiFi.begin(SSID, PASSWORD);
  Serial.print(F("Connecting to WiFi"));
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  server.on("/",          handleRoot);
  server.on("/forward",   handleForward);
  server.on("/left",      handleLeft);
  server.on("/right",     handleRight);
  server.on("/backward",  handleBackward);
  server.on("/stop",      handleStop);
  server.on("/stand",     handleStand);
  server.on("/bigleft",   handleBigLeft);
  server.on("/bigright",  handleBigRight);
  server.on("/pivotright",handlePivotRight);
  server.on("/pivotleft", handlePivotLeft);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("Web server started"));
}

void loop() {
  server.handleClient();
  if      (walkMode == "forward")    moveLeft();
  else if (walkMode == "left")       moveLeft();
  else if (walkMode == "right")      moveRight();
  else if (walkMode == "backward")   moveBackward();
  else if (walkMode == "bigleft")    bigTurnLeft();
  else if (walkMode == "bigright")   bigTurnRight();
  else if (walkMode == "pivotleft")  pivotLeft();
  else if (walkMode == "pivotright") pivotRight();
  delay(2);
}
