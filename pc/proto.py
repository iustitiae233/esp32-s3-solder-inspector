"""设备 ↔ PC 二进制协议(小端)。与 firmware/main/app_net/proto.h 严格对应,修改须两侧同步。

帧结构: [u32 magic][u16 type][u16 len][payload]
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Iterator

MAGIC = 0xA55A1234

TYPE_HELLO = 0x01
TYPE_IMAGE = 0x02
TYPE_DETECT = 0x03
TYPE_HEARTBEAT = 0x04
TYPE_COMMAND = 0x05

HELLO_VERSION = 1

IMG_FMT_RGB565LE = 0
IMG_HEAD_LEN = 10

DETECT_BOX_LEN = 22   # f32*4 + u8 + u8 + f32

CMD_STREAM_START = 0x01
CMD_STREAM_STOP = 0x02
CMD_SINGLE_SHOT = 0x03

FRAME_HEAD_LEN = 8
MAX_PAYLOAD = 1 << 20   # PC 侧宽松上限(设备上行图像 240*240*2 ≈ 115KB)

_HDR = struct.Struct("<IHH")


@dataclass
class Frame:
    """一条完整协议帧。"""
    type: int
    payload: bytes


def encode(ftype: int, payload: bytes = b"") -> bytes:
    """打包一帧。"""
    if len(payload) > 0xFFFF:
        raise ValueError(f"payload too long: {len(payload)}")
    return _HDR.pack(MAGIC, ftype, len(payload)) + payload


def encode_hello(name: str = "PC-Inspector") -> bytes:
    """HELLO:u8 ver, u8 rsv[3], char name[16], u32 fw_ver。"""
    p = struct.pack("<B3x16sI", HELLO_VERSION, name.encode("ascii")[:15], 0)
    return encode(TYPE_HELLO, p)


def encode_command(cmd: int, arg: int = 0) -> bytes:
    """COMMAND:u8 cmd, u8 arg。"""
    return encode(TYPE_COMMAND, struct.pack("<BB", cmd, arg))


def encode_detect(frame_id: int, boxes: list[tuple[float, float, float, float, int, float]]) -> bytes:
    """DETECT:u32 frame_id, u16 n, ×n{f32 x,y,w,h, u8 cls, u8 rsv, f32 conf}。"""
    p = struct.pack("<IH", frame_id & 0xFFFFFFFF, len(boxes))
    for x, y, w, h, cls, conf in boxes:
        p += struct.pack("<ffffBBf", x, y, w, h, cls, 0, conf)
    return encode(TYPE_DETECT, p)


def decode_image(payload: bytes) -> tuple[int, int, int, int, bytes]:
    """解 IMAGE 头:返回 (w, h, fmt, frame_id, pixels)。"""
    w, h, fmt, _rsv, fid = struct.unpack_from("<HHBBI", payload, 0)
    return w, h, fmt, fid, payload[IMG_HEAD_LEN:]


def decode_detect(payload: bytes) -> tuple[int, list[tuple[float, float, float, float, int, float]]]:
    """解 DETECT:返回 (frame_id, [(x, y, w, h, cls, conf), ...])。"""
    frame_id, n = struct.unpack_from("<IH", payload, 0)
    boxes = []
    off = 6
    for _ in range(n):
        if off + DETECT_BOX_LEN > len(payload):
            break   # 截断容错
        x, y, w, h, cls, _rsv, conf = struct.unpack_from("<ffffBBf", payload, off)
        boxes.append((x, y, w, h, cls, conf))
        off += DETECT_BOX_LEN
    return frame_id, boxes


def decode_hello(payload: bytes) -> tuple[int, str, int]:
    """解 HELLO:返回 (ver, name, fw_ver)。"""
    ver, name, fw = struct.unpack_from("<B3x16sI", payload, 0)
    return ver, name.split(b"\0", 1)[0].decode("ascii", "replace"), fw


class Decoder:
    """流式解码器:feed() 任意切片,产出完整帧。含 magic 重同步(丢垃圾字节)。"""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> Iterator[Frame]:
        self._buf += data
        while True:
            if len(self._buf) < FRAME_HEAD_LEN:
                return
            magic, ftype, plen = _HDR.unpack_from(self._buf, 0)
            if magic != MAGIC:
                # 丢 1 字节滑动重同步
                del self._buf[:1]
                continue
            if plen > MAX_PAYLOAD:
                # 非法长度:整块丢弃重新同步
                del self._buf[:]
                return
            if len(self._buf) < FRAME_HEAD_LEN + plen:
                return
            payload = bytes(self._buf[FRAME_HEAD_LEN:FRAME_HEAD_LEN + plen])
            del self._buf[:FRAME_HEAD_LEN + plen]
            yield Frame(ftype, payload)
