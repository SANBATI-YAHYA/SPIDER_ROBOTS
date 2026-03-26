import cv2
import numpy as np
import logging
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

try:
    from pyzbar import pyzbar
    PYZBAR_AVAILABLE = True
except ImportError:
    PYZBAR_AVAILABLE = False

log = logging.getLogger("Detector")

@dataclass
class ColorBlob:
    color: str
    center: Tuple[int, int]
    area: float
    bbox: Tuple[int, int, int, int]

@dataclass
class CodeResult:
    kind: str
    data: str
    center: Tuple[int, int]
    bbox: Tuple[int, int, int, int]

@dataclass
class ArUcoResult:
    id: int
    center: Tuple[int, int]
    corners: np.ndarray
    bbox: Tuple[int, int, int, int]

@dataclass
class DetectionResult:
    frame: np.ndarray
    blobs: List[ColorBlob] = field(default_factory=list)
    codes: List[CodeResult] = field(default_factory=list)
    aruco: List[ArUcoResult] = field(default_factory=list)
    annotated: Optional[np.ndarray] = None

COLOR_RANGES = {
    "red": [
        (np.array([0, 100, 80]), np.array([8, 255, 255])),
        (np.array([170, 100, 80]), np.array([179, 255, 255])),
    ],
    "green": [(np.array([40, 60, 60]), np.array([85, 255, 255]))],
    "blue": [(np.array([95, 80, 50]), np.array([135, 255, 255]))],
}

DRAW_COLOR = {
    "red": (0, 0, 220),
    "green": (0, 210, 0),
    "blue": (220, 80, 0),
    "code": (255, 200, 0),
    "aruco": (0, 255, 255)
}

class VisionDetector:
    def __init__(
        self,
        min_area: int = 800,
        blur_ksize: int = 5,
        morph_ksize: int = 7,
        draw_overlay: bool = True,
        on_detection: Optional[Callable[[DetectionResult], None]] = None,
        aruco_dict: int = cv2.aruco.DICT_4X4_50
    ):
        self.min_area = min_area
        self.blur_ksize = blur_ksize
        self.draw_overlay = draw_overlay
        self.on_detection = on_detection
        
        self._morph_kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (morph_ksize, morph_ksize))
        self._aruco_dict = cv2.aruco.getPredefinedDictionary(aruco_dict)
        self._aruco_params = cv2.aruco.DetectorParameters()
        self._aruco_detector = cv2.aruco.ArucoDetector(self._aruco_dict, self._aruco_params)

    def process(self, frame: np.ndarray) -> DetectionResult:
        blurred = cv2.GaussianBlur(frame, (self.blur_ksize, self.blur_ksize), 0)
        hsv = cv2.cvtColor(blurred, cv2.COLOR_BGR2HSV)

        blobs = self._detect_colors(hsv)
        codes = self._detect_codes(frame) if PYZBAR_AVAILABLE else []
        arucos = self._detect_aruco(frame)

        result = DetectionResult(frame=frame, blobs=blobs, codes=codes, aruco=arucos)

        if self.draw_overlay:
            result.annotated = self._draw(frame.copy(), blobs, codes, arucos)

        if self.on_detection:
            self.on_detection(result)

        return result

    def _detect_colors(self, hsv: np.ndarray) -> List[ColorBlob]:
        blobs = []
        for name, ranges in COLOR_RANGES.items():
            mask = np.zeros(hsv.shape[:2], dtype=np.uint8)
            for lo, hi in ranges:
                mask |= cv2.inRange(hsv, lo, hi)
            
            mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, self._morph_kernel)
            mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, self._morph_kernel)
            
            contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            for cnt in contours:
                area = cv2.contourArea(cnt)
                if area < self.min_area: continue
                M = cv2.moments(cnt)
                if M["m00"] == 0: continue
                
                cx, cy = int(M["m10"]/M["m00"]), int(M["m01"]/M["m00"])
                blobs.append(ColorBlob(name, (cx, cy), area, cv2.boundingRect(cnt)))
        return blobs

    def _detect_codes(self, frame: np.ndarray) -> List[CodeResult]:
        codes = []
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        for obj in pyzbar.decode(gray):
            x, y, w, h = obj.rect
            codes.append(CodeResult(
                kind=obj.type,
                data=obj.data.decode("utf-8", errors="replace"),
                center=(x + w//2, y + h//2),
                bbox=(x, y, w, h)
            ))
        return codes

    def _detect_aruco(self, frame: np.ndarray) -> List[ArUcoResult]:
        arucos = []
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        corners, ids, _ = self._aruco_detector.detectMarkers(gray)
        
        if ids is not None:
            for i in range(len(ids)):
                c = corners[i].reshape((4, 2))
                cx, cy = int(c[:, 0].mean()), int(c[:, 1].mean())
                arucos.append(ArUcoResult(int(ids[i][0]), (cx, cy), c, cv2.boundingRect(corners[i])))
        return arucos

    def _draw(self, frame, blobs, codes, arucos) -> np.ndarray:
        for b in blobs:
            c = DRAW_COLOR[b.color]
            x, y, w, h = b.bbox
            cv2.rectangle(frame, (x, y), (x+w, y+h), c, 2)
            cv2.putText(frame, b.color, (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 2)

        for cd in codes:
            c = DRAW_COLOR["code"]
            x, y, w, h = cd.bbox
            cv2.rectangle(frame, (x, y), (x+w, y+h), c, 2)
            cv2.putText(frame, f"CODE:{cd.data[:10]}", (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 2)

        for a in arucos:
            c = DRAW_COLOR["aruco"]
            x, y, w, h = a.bbox
            cv2.rectangle(frame, (x, y), (x+w, y+h), c, 2)
            cv2.putText(frame, f"ID:{a.id}", (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 2)
            
        return frame

if __name__ == "__main__":
    detector = VisionDetector()
    cap = cv2.VideoCapture(0)
    while True:
        ret, frame = cap.read()
        if not ret: break
        res = detector.process(frame)
        cv2.imshow("Test", res.annotated if res.annotated is not None else frame)
        if cv2.waitKey(1) & 0xFF == ord('q'): break
    cap.release()
    cv2.destroyAllWindows()