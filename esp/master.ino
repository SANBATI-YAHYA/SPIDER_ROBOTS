/*
 * ================================================================
 *  master.ino — Quadruped Master Controller v2.0
 *  ESP32 + PCA9685 (I2C: SDA=21, SCL=22)
 *  8 servos on channels: 0, 2, 4, 6, 8, 10, 12, 14
 *
 *  Features:
 *    • Hard 90° init (original calibration preserved)
 *    • 2-DOF Inverse Kinematics per leg
 *    • Diagonal trot gait: FWD / BWD / LEFT / RIGHT
 *    • Stand / Sit poses
 *    • WiFi HTTP web UI (browser/phone control)
 *    • WiFi UDP socket (laptop_control.py compatible)
 *
 *  Leg layout (top view):
 *    FL(0) --- FR(1)
 *      |         |
 *    RL(2) --- RR(3)
 *
 *  PCA9685 channels:
 *    Leg   HIP   KNEE
 *    FL     0     2
 *    FR     4     6
 *    RL     8    10
 *    RR    12    14
 * ================================================================
 */

#include <Adafruit_PWMServoDriver.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <math.h>

// ── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID "Nyx"
#define WIFI_PASS "a123456a"
#define HTTP_PORT 80
#define UDP_PORT 5000      // autonomous control channel
#define ANNOUNCE_PORT 4999 // IP discovery broadcast

// ── PCA9685 ──────────────────────────────────────────────────────────────────
#define I2C_SDA 21
#define I2C_SCL 22
#define SERVOMIN 150 // pulse count → 0°
#define SERVOMAX 600 // pulse count → 180°
#define SERVO_FREQ 60

// ── Leg geometry (mm) ────────────────────────────────────────────────────────
// Measure your physical robot and set these
#define L1 55.0f // hip joint → knee joint
#define L2 65.0f // knee joint → foot tip

// ── Servo channel map ────────────────────────────────────────────────────────
const uint8_t HIP_CH[4] = {0, 4, 8, 12};
const uint8_t KNEE_CH[4] = {2, 6, 10, 14};

// Polarity: flip to -1 if a servo moves the wrong direction after assembly
const int HIP_DIR[4] = {1, -1, 1, -1};
const int KNEE_DIR[4] = {1, -1, 1, -1};

// Servo neutral = 90° corresponds to:
//   HIP  → leg hanging straight down (no fwd/bwd lean)
//   KNEE → leg at ~90° angle (calibrate to your stand height)
const int HIP_NEUTRAL[4] = {90, 90, 90, 90};
const int KNEE_NEUTRAL[4] = {90, 90, 90, 90};

// ── Foot positions (leg-local frame, mm) ─────────────────────────────────────
// x = forward(+) / backward(-) swing
// z = downward reach from hip joint
#define STAND_Z 85.0f // comfortable standing height  (< L1+L2=120)
#define STAND_X 0.0f
#define SIT_Z 45.0f // crouched sit
#define SIT_X 0.0f

// ── Gait parameters
// ───────────────────────────────────────────────────────────
#define STEP_LEN 20.0f // mm foot swing per half-step
#define STEP_H 14.0f   // mm foot lift height
#define STEP_MS 200    // ms per half-step phase

// ── Objects
// ───────────────────────────────────────────────────────────────────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
WebServer httpServer(HTTP_PORT);
WiFiUDP udp;

// ── Command enum
// ──────────────────────────────────────────────────────────────
enum Cmd {
  CMD_STOP,
  CMD_STAND,
  CMD_SIT,
  CMD_FWD,
  CMD_BWD,
  CMD_LEFT,
  CMD_RIGHT
};
volatile Cmd currentCmd = CMD_STOP;
Cmd prevCmd = CMD_STOP; // tracks last state to detect transitions

// ── Gait state
// ────────────────────────────────────────────────────────────────
int stepPhase = 0;
unsigned long lastStepMs = 0;

// =============================================================================
//  IK — 2-DOF planar inverse kinematics
//  Input : foot position (x=fwd, z=down) in mm from hip joint
//  Output: hip and knee angles in degrees, relative to neutral
// =============================================================================
struct LegAngles {
  float hip, knee;
};

LegAngles computeIK(float x, float z) {
  float R2 = x * x + z * z;
  float maxR = L1 + L2 - 0.5f;

  // Clamp to reachable envelope
  if (R2 > maxR * maxR) {
    float s = maxR / sqrtf(R2);
    x *= s;
    z *= s;
    R2 = maxR * maxR;
  }

  // Knee interior angle (law of cosines)
  float cos_k = (R2 - L1 * L1 - L2 * L2) / (2.0f * L1 * L2);
  cos_k = constrain(cos_k, -1.0f, 1.0f);
  float theta2 = acosf(cos_k); // 0=straight, π=fully folded

  // Hip angle from downward vertical
  float alpha = atan2f(x, z);
  float beta = atan2f(L2 * sinf(theta2), L1 + L2 * cos_k);
  float theta1 = alpha - beta;

  LegAngles a;
  a.hip =
      degrees(theta1); // deviation from straight-down (→ add to HIP_NEUTRAL)
  a.knee =
      degrees(theta2); // 0=extended; map 0→90 on servo via KNEE_NEUTRAL offset
  return a;
}

// =============================================================================
//  SERVO WRITE
// =============================================================================
void setServoAngle(uint8_t ch, int angle_deg) {
  angle_deg = constrain(angle_deg, 0, 180);
  int pulse = map(angle_deg, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(ch, 0, pulse);
}

void setLeg(uint8_t leg, float x, float z) {
  LegAngles a = computeIK(x, z);
  // Hip: neutral=down, add hip deviation
  int hip_ang = HIP_NEUTRAL[leg] + HIP_DIR[leg] * (int)roundf(a.hip);
  // Knee: servo 90° = leg straight (theta2=0). Add knee bend beyond 0.
  int knee_ang = KNEE_NEUTRAL[leg] + KNEE_DIR[leg] * (int)roundf(a.knee);
  setServoAngle(HIP_CH[leg], hip_ang);
  setServoAngle(KNEE_CH[leg], knee_ang);
}

void setAllLegs(float x, float z) {
  for (int i = 0; i < 4; i++)
    setLeg(i, x, z);
}

// =============================================================================
//  POSES
// =============================================================================

// Hard 90° on every servo channel — no IK, guaranteed neutral geometry.
// This IS the standing pose: all joints mid-point.
void poseStand() {
  int pulse90 = map(90, 0, 180, SERVOMIN, SERVOMAX);
  for (int i = 0; i <= 14; i += 2)
    pwm.setPWM(i, 0, pulse90);
  Serial.println("[POSE] Stand — all servos 90°");
}

void poseInit() {
  poseStand(); // boot init is the same as stand
}

void poseSit() {
  setAllLegs(SIT_X, SIT_Z);
  Serial.println("[POSE] Sit");
}

// =============================================================================
//  TROT GAIT — diagonal pairs, 2 legs on ground at all times
//
//  Pair A = FL(0) + RR(3)   |   Pair B = FR(1) + RL(2)
//
//  dx   : forward/backward mm per step (+fwd, -bwd)
//  turn : +1=right, -1=left, 0=straight
//
//  Turning works by applying asymmetric x offsets:
//    left side  gets (dx - turn * STEP_LEN * 0.5)
//    right side gets (dx + turn * STEP_LEN * 0.5)
//  When one side sweeps forward and the other sweeps back,
//  the body rotates in place.
// =============================================================================
void trotStep(float dx, float turn) {
  if (millis() - lastStepMs < STEP_MS)
    return;
  lastStepMs = millis();

  // Per-side x target (legs 0,2 = left; legs 1,3 = right)
  float lx = dx - turn * STEP_LEN * 0.5f; // left side swing target
  float rx = dx + turn * STEP_LEN * 0.5f; // right side swing target

  // Leg sides: FL=left, FR=right, RL=left, RR=right
  // Diagonal Pair A: FL(0)[left] + RR(3)[right]
  // Diagonal Pair B: FR(1)[right] + RL(2)[left]

  if (stepPhase == 0) {
    // Pair A lifts and swings to target
    setLeg(0, +lx, STAND_Z - STEP_H); // FL lift
    setLeg(3, +rx, STAND_Z - STEP_H); // RR lift
    // Pair B stays planted (push opposite)
    setLeg(1, -rx, STAND_Z); // FR planted
    setLeg(2, -lx, STAND_Z); // RL planted
  } else {
    // Pair B lifts and swings to target
    setLeg(1, +rx, STAND_Z - STEP_H); // FR lift
    setLeg(2, +lx, STAND_Z - STEP_H); // RL lift
    // Pair A stays planted
    setLeg(0, -lx, STAND_Z); // FL planted
    setLeg(3, -rx, STAND_Z); // RR planted
  }

  stepPhase ^= 1;
}

// =============================================================================
//  MOTION DISPATCHER
// =============================================================================
// =============================================================================
//  MOTION DISPATCHER
//
//  State machine rules:
//    CMD_STAND  — sticky: stays until SIT or a move cmd is received.
//                 Pose is only re-applied on state entry (not every tick).
//    CMD_SIT    — sticky: stays until STAND is received.
//    CMD_FWD/BWD/LEFT/RIGHT — active gait; STOP after these → back to STAND.
//    CMD_STOP   — returns to stand if coming from a gait, otherwise idle.
// =============================================================================
void handleMotion() {
  bool entered = (currentCmd != prevCmd); // true on state transition
  prevCmd = currentCmd;

  switch (currentCmd) {
  case CMD_FWD:
    trotStep(+STEP_LEN, 0.0f);
    break;
  case CMD_BWD:
    trotStep(-STEP_LEN, 0.0f);
    break;
  case CMD_LEFT:
    trotStep(0.0f, -1.0f);
    break;
  case CMD_RIGHT:
    trotStep(0.0f, +1.0f);
    break;

  case CMD_STAND:
    if (entered)
      poseStand(); // apply once on entry, then hold
    break;

  case CMD_SIT:
    if (entered)
      poseSit(); // apply once on entry, then hold
    break;

  case CMD_STOP:
    // If coming from a gait, return to standing so robot doesn't drop
    if (entered)
      poseStand();
    break;
  }
}

// =============================================================================
//  UDP HANDLER — port 5000, text protocol: "FWD" "BWD" "LEFT" "RIGHT" "STAND"
//  "SIT" "STOP"
// =============================================================================
void handleUDP() {
  int pkt = udp.parsePacket();
  if (pkt < 1)
    return;

  char buf[32];
  int len = udp.read(buf, sizeof(buf) - 1);
  buf[len] = '\0';

  String cmd = String(buf);
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "FWD")
    currentCmd = CMD_FWD;
  else if (cmd == "BWD")
    currentCmd = CMD_BWD;
  else if (cmd == "LEFT")
    currentCmd = CMD_LEFT;
  else if (cmd == "RIGHT")
    currentCmd = CMD_RIGHT;
  else if (cmd == "STAND")
    currentCmd = CMD_STAND;
  else if (cmd == "SIT")
    currentCmd = CMD_SIT;
  else if (cmd == "STOP")
    currentCmd = CMD_STOP;

  Serial.printf("[UDP:%d] %s\n", UDP_PORT, cmd.c_str());
}

// =============================================================================
//  HTTP WEB UI
// =============================================================================
const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Quadruped Control</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{
    background:#080810;color:#dde;
    font-family:'Courier New',monospace;
    display:flex;flex-direction:column;align-items:center;
    justify-content:center;min-height:100vh;gap:24px;
    user-select:none;
  }
  h1{font-size:1.2rem;letter-spacing:4px;color:#58a9f5;text-transform:uppercase}
  #status{font-size:.7rem;letter-spacing:2px;color:#556;min-height:1em}
  .dpad{
    display:grid;
    grid-template-columns:repeat(3,72px);
    grid-template-rows:repeat(3,72px);
    gap:8px;
  }
  .btn{
    background:#0f0f18;border:1px solid #222;border-radius:12px;
    color:#888;font-size:1.6rem;cursor:pointer;
    transition:background .1s,transform .1s,border-color .1s;
    display:flex;align-items:center;justify-content:center;
  }
  .btn:active,.btn.active{transform:scale(.9);border-color:#58a9f5}
  #btnU{background:#091a09;border-color:#1e5c1e;color:#3fa}
  #btnD{background:#1a0909;border-color:#5c1e1e;color:#f55}
  #btnL,#btnR{background:#090d1a;border-color:#1e2e5c;color:#5af}
  #btnStp{font-size:.75rem;letter-spacing:1px;background:#141400;border-color:#554;color:#cc4}
  .actions{display:flex;gap:14px}
  .act{
    padding:14px 26px;border-radius:10px;
    border:1px solid #333;background:#0c0c14;
    color:#aaa;font-family:inherit;font-size:.78rem;
    letter-spacing:2px;text-transform:uppercase;cursor:pointer;
    transition:background .15s,border-color .15s;
  }
  .act:hover{background:#161620}
  #btnStand{border-color:#58a9f5;color:#58a9f5}
  #btnSit{border-color:#fa7;color:#fa7}
</style>
</head>
<body>
<h1>&#x1F916; Quadruped</h1>
<div id="status">IDLE</div>

<div class="dpad">
  <div></div>
  <div class="btn" id="btnU"   onpointerdown="go('FWD')"  onpointerup="go('STOP')">&#x25B2;</div>
  <div></div>
  <div class="btn" id="btnL"   onpointerdown="go('LEFT')" onpointerup="go('STOP')">&#x25C4;</div>
  <div class="btn" id="btnStp" onpointerdown="go('STOP')">STP</div>
  <div class="btn" id="btnR"   onpointerdown="go('RIGHT')"onpointerup="go('STOP')">&#x25BA;</div>
  <div></div>
  <div class="btn" id="btnD"   onpointerdown="go('BWD')"  onpointerup="go('STOP')">&#x25BC;</div>
  <div></div>
</div>

<div class="actions">
  <button class="act" id="btnStand" onclick="go('STAND')">STAND</button>
  <button class="act" id="btnSit"   onclick="go('SIT')">SIT</button>
</div>

<script>
function go(cmd){
  document.getElementById('status').textContent = cmd;
  fetch('/cmd?c='+cmd).catch(()=>{});
}
const K = {ArrowUp:'FWD',ArrowDown:'BWD',ArrowLeft:'LEFT',ArrowRight:'RIGHT',' ':'STOP',s:'STAND',x:'SIT'};
const held = new Set();
document.addEventListener('keydown',e=>{
  if(K[e.key] && !held.has(e.key)){held.add(e.key);e.preventDefault();go(K[e.key]);}
});
document.addEventListener('keyup',e=>{
  held.delete(e.key);
  if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight'].includes(e.key)) go('STOP');
});
</script>
</body>
</html>
)html";

void setupHTTP() {
  httpServer.on("/", HTTP_GET,
                []() { httpServer.send_P(200, "text/html", INDEX_HTML); });

  httpServer.on("/cmd", HTTP_GET, []() {
    String cmd = httpServer.arg("c");
    cmd.toUpperCase();
    cmd.trim();
    if (cmd == "FWD")
      currentCmd = CMD_FWD;
    else if (cmd == "BWD")
      currentCmd = CMD_BWD;
    else if (cmd == "LEFT")
      currentCmd = CMD_LEFT;
    else if (cmd == "RIGHT")
      currentCmd = CMD_RIGHT;
    else if (cmd == "STAND")
      currentCmd = CMD_STAND;
    else if (cmd == "SIT")
      currentCmd = CMD_SIT;
    else if (cmd == "STOP")
      currentCmd = CMD_STOP;
    httpServer.send(200, "text/plain", "OK");
    Serial.printf("[HTTP] %s\n", cmd.c_str());
  });

  httpServer.begin();
  Serial.printf("[HTTP] Web UI → http://%s/\n",
                WiFi.localIP().toString().c_str());
}

// =============================================================================
//  IP ANNOUNCEMENT — broadcasts "QUAD:<ip>" every 3 s so the laptop can
//  discover the robot without needing Serial.
// =============================================================================
WiFiUDP announceUdp;
unsigned long lastAnnounceMs = 0;
#define ANNOUNCE_INTERVAL_MS 3000

void announceIP() {
  String msg = "QUAD:" + WiFi.localIP().toString();
  announceUdp.beginPacket(IPAddress(255, 255, 255, 255), ANNOUNCE_PORT);
  announceUdp.print(msg);
  announceUdp.endPacket();
  Serial.printf("[ANNOUNCE] %s → broadcast:%d\n", msg.c_str(), ANNOUNCE_PORT);
}

// =============================================================================
//  WIFI
// =============================================================================
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Connecting");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 15000) {
      Serial.println("\n[WiFi] TIMEOUT — check SSID/PASS");
      return;
    }
  }
  Serial.printf("\n[WiFi] Connected → %s\n", WiFi.localIP().toString().c_str());
}

// =============================================================================
//  SETUP / LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Quadruped Master v2.0 ===");

  Wire.begin(I2C_SDA, I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
  delay(100);

  // Hard 90° init — all servos simultaneously, no ramping
  poseInit();
  delay(500);
  Serial.println("[INIT] All servos → 90°");

  connectWiFi();
  udp.begin(UDP_PORT);
  announceUdp.begin(ANNOUNCE_PORT); // TX only — bind needed for send
  Serial.printf("[UDP]  Listening on port %d\n", UDP_PORT);
  setupHTTP();
  announceIP(); // immediate first broadcast

  // Stand up after boot
  currentCmd = CMD_STAND;
}

void loop() {
  httpServer.handleClient();
  handleUDP();
  handleMotion();

  // Periodic IP broadcast
  if (millis() - lastAnnounceMs >= ANNOUNCE_INTERVAL_MS) {
    lastAnnounceMs = millis();
    announceIP();
  }
}