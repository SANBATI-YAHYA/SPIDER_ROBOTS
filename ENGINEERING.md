# Engineering Documentation: SRG-Comp26 Vision & Communication System

This document outlines the technical implementation of the UDP server and color detection system for the SRG-Hackathon quadruped robot.

---

## 1. Communication Architecture (UDP)

The system uses a custom binary protocol over UDP to minimize latency. Communication is split into two primary channels.

### Ports
- **Video/Telemetry Port (Inbound):** `5005`
  - ESP32 → PC
  - Handles JPEG frame chunks and sensor telemetry.
- **Command Port (Outbound):** `5006`
  - PC → ESP32
  - Handles movement commands and configuration handshakes.

### Handshake Protocol
Before streaming begins, a configuration handshake occurs:
1. **Server Detection:** The server waits for any packet from the ESP32 to identify its IP.
2. **Config Request:** Server sends `MAGIC_HANDSHAKE (0x1122)` followed by 3 bytes: `[ResolutionID, FPS, Quality]`.
3. **Acknowledgment:** ESP32 applies settings and replies with `MAGIC_ACK (0x3344)`.
4. **Retries:** The server retransmits the handshake every 1.0s until an ACK is received.

### Video Streaming (Chunked JPEG)
Since UDP packets have a size limit (MTU, though often ~1400 bytes in practice, Python allows up to 65507), large JPEG frames are split into chunks:
- **Magic:** `0xAABB`
- **Header:** `seq_id (2 bytes) | chunk_id (1 byte) | total_chunks (1 byte)`
- **Payload:** Raw JPEG bytes.
The server reassembles these chunks using the `seq_id`. If chunks are lost or delayed beyond `FRAME_TIMEOUT (3.0s)`, the partial frame is dropped.

### Telemetry (Sensors)
- **Magic:** `0xEEFF`
- **Payload:** `timestamp (float) | battery_mv (uint16) | roll (float) | pitch (float) | yaw (float)`

---

## 2. Computer Vision System

The `VisionDetector` class handles real-time image processing using OpenCV.

### Video Quality & Performance
- **Resolution:** 640x480 (VGA) - Res ID `8`.
- **JPEG Quality:** `12` (Lower = better quality, `0` is best, `63` is worst).
- **Requested FPS:** `15`. 
  - **Note:** The `fps` argument represents the *requested* capture rate from the ESP32-CAM. Actual throughput depends on network stability and ESP32 processing overhead.

### Color Detection (HSV Ranges)
Colors are detected by converting frames to the **HSV (Hue, Saturation, Value)** color space. This provides better robustness against lighting changes compared to RGB.

| Color | Hue Range (Lo) | Hue Range (Hi) | Saturation/Value Constraints |
| :--- | :--- | :--- | :--- |
| **Red** | `0-8` & `170-179` | `0-8` & `170-179` | S > 100, V > 80 |
| **Green** | `40` | `85` | S > 60, V > 60 |
| **Blue** | `95` | `135` | S > 80, V > 50 |

#### Processing Pipeline:
1. **Gaussian Blur:** 5x5 kernel to reduce high-frequency noise.
2. **Thresholding:** `cv2.inRange` creates a binary mask for each color.
3. **Morphology:** 
   - **Opening:** Removes small noise (erosion followed by dilation).
   - **Closing:** Fills small holes in detected blobs (dilation followed by erosion).
4. **Contour Filtering:** Blobs smaller than **800 pixels** are ignored to prevent false positives from background noise.
5. **Centroid Calculation:** Calculated via Image Moments ($M_{10}/M_{00}, M_{01}/M_{00}$).

### Additional Detectors
- **QR Codes:** Integrated via `pyzbar`. Supports various barcode types, primarily filtered for `QRCODE`.
- **ArUco Markers:** Uses the `cv2.aruco` module with the `DICT_4X4_50` dictionary for precise pose estimation and ID tracking.

---

## 3. Control Logic

The UDP server includes a command API (`send_command`) to drive the robot:
- `cmd_move(vx, vy, vz)`: Linear movement in 3D space.
- `cmd_turn(yaw_rate)`: Rotation.
- `cmd_follow(tx, ty, depth)`: Automatic target tracking (Normalized coordinates -0.5 to 0.5).

The current implementation in `udp_server.py` includes a detection hook that calculates the normalized error of the largest detected blob and sends `cmd_follow` commands to center the robot on the target.
