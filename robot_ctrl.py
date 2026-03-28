"""
robot_ctrl.py — Autonomous UDP controller for the quadruped master.
Protocol: plain text commands over UDP port 5000.

Usage (standalone demo):
    python robot_ctrl.py --ip 192.168.x.x

Usage (as a module):
    from robot_ctrl import RobotController
    bot = RobotController("192.168.x.x")
    bot.stand()
    bot.forward(1.5)   # walk forward for 1.5 seconds
    bot.left(0.8)
    bot.stop()
"""

import socket
import time
import argparse
import logging

log = logging.getLogger("RobotCtrl")
logging.basicConfig(level=logging.INFO, format="[%(asctime)s] %(message)s", datefmt="%H:%M:%S")

ROBOT_PORT = 5000


class RobotController:
    """
    Thin UDP wrapper for the quadruped master firmware.

    Commands are fire-and-forget plain text strings.
    The ESP32 holds the last received command until a new one arrives.
    So 'forward(2.0)' = send FWD, sleep 2 s, send STOP.
    """

    CMDS = {"fwd", "bwd", "left", "right", "stand", "sit", "stop"}

    def __init__(self, robot_ip: str, port: int = ROBOT_PORT, timeout: float = 2.0):
        self.addr = (robot_ip, port)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(timeout)
        log.info(f"RobotController → {robot_ip}:{port}")

    # ── Low-level ────────────────────────────────────────────────────────────

    def send(self, cmd: str) -> None:
        """Send a raw command string. Case-insensitive."""
        cmd = cmd.strip().upper()
        if cmd.lower() not in self.CMDS:
            raise ValueError(f"Unknown command '{cmd}'. Valid: {self.CMDS}")
        self._sock.sendto(cmd.encode(), self.addr)
        log.info(f"→ {cmd}")

    def close(self):
        self._sock.close()

    # ── Context manager support ───────────────────────────────────────────────

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.stop()
        self.close()

    # ── Instant commands ──────────────────────────────────────────────────────

    def stop(self):   self.send("STOP")
    def stand(self):  self.send("STAND")
    def sit(self):    self.send("SIT")

    # ── Timed movement helpers ────────────────────────────────────────────────

    def _move(self, cmd: str, duration: float) -> None:
        """Send cmd, hold for duration seconds, then stop."""
        self.send(cmd)
        time.sleep(duration)
        self.send("STOP")

    def forward(self, duration: float = 1.0):
        """Walk forward for `duration` seconds."""
        self._move("FWD", duration)

    def backward(self, duration: float = 1.0):
        """Walk backward for `duration` seconds."""
        self._move("BWD", duration)

    def left(self, duration: float = 0.5):
        """Turn left for `duration` seconds."""
        self._move("LEFT", duration)

    def right(self, duration: float = 0.5):
        """Turn right for `duration` seconds."""
        self._move("RIGHT", duration)

    # ── Compound manoeuvres ───────────────────────────────────────────────────

    def spin(self, direction: str = "left", rotations: float = 1.0, turn_duration: float = 0.8):
        """
        Approximate a full spin.
        tune turn_duration = time needed for a ~90° turn on your surface.
        """
        steps = int(rotations * 4)
        cmd   = "LEFT" if direction.lower() == "left" else "RIGHT"
        for _ in range(steps):
            self._move(cmd, turn_duration)
            time.sleep(0.05)

    def square(self, side_duration: float = 1.5, turn_duration: float = 0.7):
        """Walk a square pattern."""
        for _ in range(4):
            self.forward(side_duration)
            time.sleep(0.1)
            self.right(turn_duration)
            time.sleep(0.1)


# =============================================================================
#  INTERACTIVE REPL  (run directly: python robot_ctrl.py --ip 192.168.x.x)
# =============================================================================

HELP = """
Commands:
  fwd [s]    — walk forward (default 1 s)
  bwd [s]    — walk backward
  left [s]   — turn left
  right [s]  — turn right
  stand      — stand up pose
  sit        — sit down pose
  stop       — halt immediately
  spin       — 360° left spin
  square     — walk a square
  q / quit   — exit
"""

def repl(bot: RobotController):
    print(HELP)
    while True:
        try:
            line = input("cmd> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            break

        if not line:
            continue

        parts = line.split()
        verb  = parts[0]
        dur   = float(parts[1]) if len(parts) > 1 else 1.0

        if verb in ("q", "quit", "exit"):
            break
        elif verb == "fwd":     bot.forward(dur)
        elif verb == "bwd":     bot.backward(dur)
        elif verb == "left":    bot.left(dur)
        elif verb == "right":   bot.right(dur)
        elif verb == "stand":   bot.stand()
        elif verb == "sit":     bot.sit()
        elif verb == "stop":    bot.stop()
        elif verb == "spin":    bot.spin()
        elif verb == "square":  bot.square()
        else:
            print(f"  Unknown: '{verb}'  {HELP}")


# =============================================================================
#  EXAMPLE AUTONOMOUS SEQUENCE (edit to your needs)
# =============================================================================

def demo_sequence(bot: RobotController):
    log.info("=== Demo sequence start ===")

    bot.stand()
    time.sleep(1.0)

    bot.forward(2.0)
    time.sleep(0.2)

    bot.right(0.7)
    time.sleep(0.2)

    bot.forward(2.0)
    time.sleep(0.2)

    bot.spin("left", rotations=0.5)
    time.sleep(0.2)

    bot.backward(1.0)
    time.sleep(0.2)

    bot.sit()
    time.sleep(1.0)

    log.info("=== Demo sequence done ===")


# =============================================================================

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Quadruped UDP controller")
    parser.add_argument("--ip",   required=True,  help="ESP32 IP address")
    parser.add_argument("--port", default=5000,   type=int, help="UDP port (default 5000)")
    parser.add_argument("--demo", action="store_true", help="Run demo sequence then exit")
    args = parser.parse_args()

    with RobotController(args.ip, args.port) as bot:
        if args.demo:
            demo_sequence(bot)
        else:
            repl(bot)
