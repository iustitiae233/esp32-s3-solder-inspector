"""proto.py 单元测试:roundtrip、前后夹垃圾字节的重同步、截断帧。"""

import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import proto  # noqa: E402


def test_roundtrip_all_types():
    for ftype, payload in [
        (proto.TYPE_HELLO, b"\x01\x00\x00\x00" + b"name" * 4 + b"\x00" * 5),
        (proto.TYPE_IMAGE, bytes(10 + 16)),
        (proto.TYPE_DETECT, struct.pack("<IH", 7, 0)),
        (proto.TYPE_HEARTBEAT, struct.pack("<I", 123)),
        (proto.TYPE_COMMAND, bytes([proto.CMD_STREAM_START, 0])),
    ]:
        raw = proto.encode(ftype, payload)
        frames = list(proto.Decoder().feed(raw))
        assert len(frames) == 1
        assert frames[0].type == ftype
        assert frames[0].payload == payload


def test_resync_with_garbage():
    """前后夹垃圾字节 + 多帧粘连,解码器应只吐完整帧。"""
    good = proto.encode(proto.TYPE_HEARTBEAT, struct.pack("<I", 1))
    stream = b"\xde\xad\xbe\xef" + good + b"\x00" + good + good + b"\xca\xfe"
    frames = list(proto.Decoder().feed(stream))
    assert len(frames) == 3
    assert all(f.type == proto.TYPE_HEARTBEAT for f in frames)


def test_partial_feed():
    """分多次 feed 半帧,拼接后应解出。"""
    raw = proto.encode(proto.TYPE_IMAGE, bytes(10 + 64))
    d = proto.Decoder()
    out = list(d.feed(raw[:5]))
    assert not out
    out += list(d.feed(raw[5:40]))
    assert not out
    out += list(d.feed(raw[40:]))
    assert len(out) == 1 and len(out[0].payload) == 74


def test_detect_roundtrip():
    boxes = [(10.0, 20.0, 30.0, 40.0, 3, 0.87), (1.5, 2.5, 3.5, 4.5, 0, 0.99)]
    raw = proto.encode_detect(42, boxes)
    frames = list(proto.Decoder().feed(raw))
    assert len(frames) == 1
    fid, got = proto.decode_detect(frames[0].payload)
    assert fid == 42
    assert len(got) == 2
    for a, b in zip(got, boxes):
        assert a[:5] == b[:5]                       # 坐标 f32 恰好精确
        assert math.isclose(a[5], b[5], rel_tol=1e-6)


def test_image_roundtrip():
    pixels = b"\x11\x22\x33\x44"
    p = struct.pack("<HHBBI", 240, 240, proto.IMG_FMT_RGB565LE, 0, 9) + pixels
    raw = proto.encode(proto.TYPE_IMAGE, p)
    f = next(iter(proto.Decoder().feed(raw)))
    w, h, fmt, fid, px = proto.decode_image(f.payload)
    assert (w, h, fmt, fid, px) == (240, 240, proto.IMG_FMT_RGB565LE, 9, pixels)


def test_hello_roundtrip():
    raw = proto.encode_hello("PCB-Lab")
    f = next(iter(proto.Decoder().feed(raw)))
    ver, name, fw = proto.decode_hello(f.payload)
    assert ver == proto.HELLO_VERSION and name == "PCB-Lab" and fw == 0


def test_oversize_payload_drops_buffer(monkeypatch):
    """非法长度触发整块丢弃,解码器不崩溃且可继续。"""
    monkeypatch.setattr(proto, "MAX_PAYLOAD", 16)
    bad = struct.pack("<IHH", proto.MAGIC, proto.TYPE_IMAGE, 64)
    d = proto.Decoder()
    assert not list(d.feed(bad + b"junk"))
    good = proto.encode(proto.TYPE_HEARTBEAT, struct.pack("<I", 5))
    assert len(list(d.feed(good))) == 1
