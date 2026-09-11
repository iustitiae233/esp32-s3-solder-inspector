#!/usr/bin/env python3
"""焊点缺陷检测仪 PC 端(TCP Server + YOLOv8 推理 + PySide6 GUI)。

- 监听 0.0.0.0:3333(默认),设备为客户端
- IMAGE 帧 → RGB565LE 解码 → 显示(可叠加 bbox)
- YOLOv8 推理(可选)→ DETECT 帧回传设备
- 采集归档:当前帧存 pc/dataset/<label>/

用法: python inspector_pc.py [--port 3333]
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import numpy as np
from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtGui import QColor, QFont, QImage, QPainter, QPen, QPixmap
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QGridLayout, QGroupBox,
    QHBoxLayout, QLabel, QMainWindow, QPushButton, QSlider,
    QSplitter, QTextEdit, QVBoxLayout, QWidget,
)
from struct import error as struct_error

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto  # noqa: E402

HERE = Path(__file__).resolve().parent
DATASET_DIR = HERE / "dataset"

CLS_COLORS = [
    QColor(46, 204, 113),    # 0 OK 绿
    QColor(230, 126, 34),    # 1 多锡 橙
    QColor(52, 152, 219),    # 2 少锡 蓝
    QColor(231, 76, 60),     # 3 连锡 红
    QColor(155, 89, 182),    # 4 偏移 紫
    QColor(241, 196, 15),    # 5 虚焊 黄
]


def load_labels() -> list[str]:
    p = HERE / "labelmap.txt"
    if p.exists():
        lines = [l.strip() for l in p.read_text(encoding="utf-8").splitlines()]
        names = [l for l in lines if l]
        if names:
            return names
    return ["OK", "多锡", "少锡", "连锡", "偏移", "虚焊"]


def rgb565le_to_rgb888(pixels: bytes, w: int, h: int) -> np.ndarray:
    """RGB565 小端 → RGB888 (h, w, 3) uint8。"""
    arr = np.frombuffer(pixels, dtype="<u2", count=w * h).reshape(h, w)
    out = np.empty((h, w, 3), dtype=np.uint8)
    out[..., 0] = ((arr >> 11) & 0x1F) * 255 // 31
    out[..., 1] = ((arr >> 5) & 0x3F) * 255 // 63
    out[..., 2] = (arr & 0x1F) * 255 // 31
    return out


# ---------------------------------------------------------------- 设备链路

class DeviceLink(QObject):
    """TCP server:后台线程 accept/recv,经 Qt 信号投递到 GUI 线程。"""

    status_changed = Signal(str)          # 服务/连接状态文本
    hello_received = Signal(str)          # 设备名
    frame_received = Signal(int, int, QImage)   # (frame_id, 宽, 图像)
    log = Signal(str)

    def __init__(self, port: int) -> None:
        super().__init__()
        self.port = port
        self._stop = threading.Event()
        self._send_lock = threading.Lock()
        self._conn: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self.last_hb = 0.0
        self._last_rgb: tuple[int, np.ndarray] | None = None   # 供采集/推理

    # ---- 生命周期 ----

    def start(self) -> None:
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        if self._conn:
            try:
                self._conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        if self._thread:
            self._thread.join(timeout=2)

    # ---- 发送 ----

    def send(self, raw: bytes) -> bool:
        with self._send_lock:
            c = self._conn
            if c is None:
                return False
            try:
                c.sendall(raw)
                return True
            except OSError:
                return False

    def send_command(self, cmd: int, arg: int = 0) -> None:
        if self.send(proto.encode_command(cmd, arg)):
            self.log.emit(f"→ COMMAND {cmd:02X} arg={arg}")

    # ---- 服务循环(后台线程) ----

    def _serve(self) -> None:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            srv.bind(("0.0.0.0", self.port))
            srv.listen(1)
            srv.settimeout(1.0)
            self.status_changed.emit(f"监听 0.0.0.0:{self.port},等待设备…")
        except OSError as e:
            self.status_changed.emit(f"监听失败: {e}")
            return

        while not self._stop.is_set():
            try:
                conn, addr = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with conn:
                self._conn = conn
                self.status_changed.emit(f"设备已连接 {addr[0]}")
                self.log.emit(f"设备接入:{addr[0]}:{addr[1]}")
                conn.settimeout(1.0)
                dec_buf = proto.Decoder()
                while not self._stop.is_set():
                    try:
                        chunk = conn.recv(65536)
                    except socket.timeout:
                        # 心跳超时判定
                        if self.last_hb and time.time() - self.last_hb > 10:
                            self.status_changed.emit("设备心跳超时,断开")
                            break
                        continue
                    except OSError:
                        break
                    if not chunk:
                        break
                    for fr in dec_buf.feed(chunk):
                        self._handle(conn, fr)
                self._conn = None
                self.status_changed.emit("设备已断开,等待重连…")
                self.log.emit("设备断开")
        srv.close()

    def _handle(self, conn: socket.socket, fr: proto.Frame) -> None:
        if fr.type == proto.TYPE_HELLO:
            try:
                _v, name, fw = proto.decode_hello(fr.payload)
                self.hello_received.emit(f"{name} v{(fw >> 16) & 0xFF}.{(fw >> 8) & 0xFF}.{fw & 0xFF}")
            except struct_error:
                pass
        elif fr.type == proto.TYPE_HEARTBEAT:
            self.last_hb = time.time()
        elif fr.type == proto.TYPE_IMAGE:
            w, h, fmt, fid, pixels = proto.decode_image(fr.payload)
            if fmt != proto.IMG_FMT_RGB565LE or len(pixels) < w * h * 2:
                self.log.emit(f"帧 {fid}:格式非法(fmt={fmt})")
                return
            rgb = rgb565le_to_rgb888(pixels, w, h)
            img = QImage(rgb.data, w, h, w * 3, QImage.Format.Format_RGB888).copy()
            self.frame_received.emit(fid, w, img)
            self._last_rgb = (fid, rgb)     # 供采集/推理


class YoloRunner:
    """ultralytics YOLOv8 包装(懒加载,失败允许无模型运行)。"""

    def __init__(self) -> None:
        self.model = None
        self.path = ""

    def load(self, path: str) -> str:
        from ultralytics import YOLO   # 延迟导入
        self.model = YOLO(path)
        self.path = path
        return Path(path).name

    def infer(self, rgb: np.ndarray, conf: float) -> list[tuple[float, float, float, float, int, float]]:
        """返回 [(x, y, w, h, cls, conf)](xywh, 像素坐标)。"""
        if self.model is None:
            return []
        r = self.model.predict(source=rgb, conf=conf, verbose=False)[0]
        boxes = []
        for b in r.boxes:
            x1, y1, x2, y2 = b.xyxy[0].tolist()
            boxes.append((x1, y1, x2 - x1, y2 - y1, int(b.cls.item()), float(b.conf.item())))
        return boxes


# ---------------------------------------------------------------- GUI

class MainWindow(QMainWindow):
    def __init__(self, port: int) -> None:
        super().__init__()
        self.setWindowTitle("PCB 焊点缺陷检测台")
        self.resize(1060, 720)

        self.labels = load_labels()
        self.link = DeviceLink(port)
        self.yolo = YoloRunner()
        self.last_boxes: list[tuple[float, float, float, float, int, float]] = []
        self.frame_count = 0
        self._t0 = time.time()
        self._rgb: tuple[int, np.ndarray] | None = None
        self.counts: dict[int, int] = {}

        self._build_ui()
        self._connect_signals()
        self.link.start()

        hb_timer = QTimer(self)
        hb_timer.setInterval(2000)
        hb_timer.timeout.connect(self._update_hb)
        hb_timer.start()

    # ---------- UI ----------

    def _build_ui(self) -> None:
        root = QSplitter(Qt.Orientation.Horizontal)

        # 左:画面
        left = QWidget()
        lv = QVBoxLayout(left)
        self.video = QLabel("等待视频流…(设备端开启\"PC 模式/连续\")")
        self.video.setMinimumSize(480, 480)
        self.video.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.video.setStyleSheet("background:#10141c;color:#9AA3AF;")
        lv.addWidget(self.video)

        self.lbl_frame_info = QLabel("帧:--  FPS:--")
        lv.addWidget(self.lbl_frame_info)
        root.addWidget(left)

        # 右:控制面板
        right = QWidget()
        rv = QVBoxLayout(right)

        # ① 连接
        g_conn = QGroupBox("① 连接")
        cf = QGridLayout(g_conn)
        self.lbl_link_status = QLabel("未启动")
        self.lbl_device = QLabel("设备:--")
        self.lbl_hb = QLabel("心跳:--")
        cf.addWidget(self.lbl_link_status, 0, 0)
        cf.addWidget(self.lbl_device, 1, 0)
        cf.addWidget(self.lbl_hb, 2, 0)
        rv.addWidget(g_conn)

        # ② 推理控制
        g_inf = QGroupBox("② YOLO 推理")
        inf = QGridLayout(g_inf)
        self.btn_model = QPushButton("加载模型(.pt)…")
        self.lbl_model = QLabel("模型:未加载")
        self.chk_infer = QCheckBox("自动推理")
        self.chk_infer.setChecked(True)
        self.chk_send = QCheckBox("结果回传设备")
        self.chk_send.setChecked(True)
        self.chk_draw = QCheckBox("显示检测框")
        self.chk_draw.setChecked(True)
        self.sld_conf = QSlider(Qt.Orientation.Horizontal)
        self.sld_conf.setRange(5, 95)
        self.sld_conf.setValue(60)
        self.lbl_conf = QLabel("置信度 0.60")
        inf.addWidget(self.btn_model, 0, 0, 1, 2)
        inf.addWidget(self.lbl_model, 1, 0, 1, 2)
        inf.addWidget(self.chk_infer, 2, 0)
        inf.addWidget(self.chk_draw, 2, 1)
        inf.addWidget(self.chk_send, 3, 0)
        inf.addWidget(self.sld_conf, 4, 0)
        inf.addWidget(self.lbl_conf, 4, 1)
        rv.addWidget(g_inf)

        # ③ 设备控制
        g_dev = QGroupBox("③ 设备控制")
        dv = QHBoxLayout(g_dev)
        self.btn_stream = QPushButton("开始取流")
        self.btn_shot = QPushButton("单次检测")
        dv.addWidget(self.btn_stream)
        dv.addWidget(self.btn_shot)
        rv.addWidget(g_dev)

        # ④ 采集归档
        g_cap = QGroupBox("④ 采集归档(训练数据)")
        cap = QGridLayout(g_cap)
        self.cmb_label = QComboBox()
        self.cmb_label.addItems(self.labels)
        self.btn_save = QPushButton("保存当前帧")
        self.lbl_save = QLabel("")
        cap.addWidget(QLabel("类别:"), 0, 0)
        cap.addWidget(self.cmb_label, 0, 1)
        cap.addWidget(self.btn_save, 1, 0, 1, 2)
        cap.addWidget(self.lbl_save, 2, 0, 1, 2)
        rv.addWidget(g_cap)

        # ⑤ 统计/日志
        g_log = QGroupBox("⑤ 统计与日志")
        lv2 = QVBoxLayout(g_log)
        self.lbl_stats = QLabel("今日判定:--")
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setFont(QFont("Consolas", 9))
        lv2.addWidget(self.lbl_stats)
        lv2.addWidget(self.txt_log)
        rv.addWidget(g_log, stretch=1)

        root.addWidget(right)
        root.setStretchFactor(0, 3)
        root.setStretchFactor(1, 2)
        self.setCentralWidget(root)

    # ---------- 信号 ----------

    def _connect_signals(self) -> None:
        self.link.status_changed.connect(self.lbl_link_status.setText)
        self.link.hello_received.connect(lambda n: self.lbl_device.setText(f"设备:{n}"))
        self.link.frame_received.connect(self.on_frame)
        self.link.log.connect(self._log)

        self.btn_model.clicked.connect(self.on_load_model)
        self.sld_conf.valueChanged.connect(
            lambda v: self.lbl_conf.setText(f"置信度 {v / 100:.2f}"))
        self.btn_stream.clicked.connect(self.on_stream_toggle)
        self.btn_shot.clicked.connect(lambda: self.link.send_command(proto.CMD_SINGLE_SHOT))
        self.btn_save.clicked.connect(self.on_save_frame)

    def _log(self, msg: str) -> None:
        ts = datetime.now().strftime("%H:%M:%S")
        self.txt_log.append(f"[{ts}] {msg}")

    # ---------- 回调(GUI 线程) ----------

    def on_frame(self, fid: int, _w: int, img: QImage) -> None:
        self.frame_count += 1
        now = time.time()
        if now - self._t0 >= 1.0:
            fps = self.frame_count / (now - self._t0)
            self.frame_count = 0
            self._t0 = now
            self.lbl_frame_info.setText(f"帧:{fid}  FPS:{fps:.1f}")
        else:
            self.lbl_frame_info.setText(f"帧:{fid}")

        # 缓存解码帧(供推理/采集),帧号一致才用
        rgb_state = self.link._last_rgb
        if rgb_state is not None and rgb_state[0] == fid:
            self._rgb = rgb_state

        # 推理(模型已加载且勾选)
        self.last_boxes = []
        if self.chk_infer.isChecked() and self.yolo.model is not None and self._rgb is not None:
            try:
                self.last_boxes = self.yolo.infer(self._rgb[1], self.sld_conf.value() / 100)
            except Exception as e:   # 推理失败不阻断显示
                self._log(f"推理失败:{e}")
            for _x, _y, _w, _h, cls, _c in self.last_boxes:
                self.counts[cls] = self.counts.get(cls, 0) + 1
            self._update_stats()
            if self.chk_send.isChecked():
                if self.link.send(proto.encode_detect(fid, self.last_boxes)):
                    self._log(f"→ DETECT 帧{fid} {len(self.last_boxes)}框")

        # 显示
        if self.chk_draw.isChecked() and self.last_boxes:
            img = self._draw_boxes(img, self.last_boxes)
        pm = QPixmap.fromImage(img).scaled(
            self.video.width(), self.video.height(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation)
        self.video.setPixmap(pm)

    def _draw_boxes(self, img: QImage, boxes) -> QImage:
        img = img.copy()
        p = QPainter(img)
        for x, y, w, h, cls, conf in boxes:
            color = CLS_COLORS[cls % len(CLS_COLORS)]
            pen = QPen(color, 2)
            p.setPen(pen)
            p.drawRect(int(x), int(y), int(w), int(h))
            name = self.labels[cls] if cls < len(self.labels) else f"cls{cls}"
            p.fillRect(int(x), max(0, int(y) - 16), 8 * len(name) + 30, 16, color)
            p.setPen(QColor(0, 0, 0))
            p.drawText(int(x) + 2, max(11, int(y) - 4), f"{name} {conf:.0%}")
        p.end()
        return img

    def on_load_model(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "选择 YOLOv8 模型", str(HERE), "YOLO (*.pt)")
        if not path:
            return
        try:
            name = self.yolo.load(path)
            self.lbl_model.setText(f"模型:{name}")
            self._log(f"模型已加载:{path}")
        except Exception as e:
            self.lbl_model.setText("模型:加载失败")
            self._log(f"模型加载失败:{e}")

    def on_stream_toggle(self) -> None:
        if self.btn_stream.text().startswith("开始"):
            self.link.send_command(proto.CMD_STREAM_START)
            self.btn_stream.setText("停止取流")
        else:
            self.link.send_command(proto.CMD_STREAM_STOP)
            self.btn_stream.setText("开始取流")

    def on_save_frame(self) -> None:
        if self._rgb is None:
            self.lbl_save.setText("无帧可保存(先取流)")
            return
        label = self.cmb_label.currentText()
        d = DATASET_DIR / label
        d.mkdir(parents=True, exist_ok=True)
        name = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3] + ".png"
        fid, rgb = self._rgb
        img = QImage(rgb.data, rgb.shape[1], rgb.shape[0],
                     rgb.shape[1] * 3, QImage.Format.Format_RGB888).copy()
        if img.save(str(d / name), "PNG"):
            self.lbl_save.setText(f"已存 dataset/{label}/{name}")
            self._log(f"采集 {label}/{name} (帧{fid})")
        else:
            self.lbl_save.setText("保存失败")

    def _update_stats(self) -> None:
        total = sum(self.counts.values())
        parts = [f"{self.labels[c]}:{n}" for c, n in sorted(self.counts.items())
                 if c < len(self.labels)]
        self.lbl_stats.setText(f"共推理判定:{total}  " + "  ".join(parts))

    def _update_hb(self) -> None:
        hb = self.link.last_hb
        if hb:
            age = time.time() - hb
            self.lbl_hb.setText(f"心跳:{age:.1f}s 前")
        else:
            self.lbl_hb.setText("心跳:--")

    def closeEvent(self, e) -> None:
        self.link.stop()
        super().closeEvent(e)


def main() -> int:
    ap = argparse.ArgumentParser(description="PCB 焊点缺陷检测台(PC 端)")
    ap.add_argument("--port", type=int, default=3333, help="监听端口(默认 3333)")
    args = ap.parse_args()
    app = QApplication(sys.argv)
    win = MainWindow(args.port)
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
