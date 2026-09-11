#!/usr/bin/env python3
"""把 pc/export/{model_int8.tflite, labels.txt} 打包成 FATFS(WL)镜像并烧录到 storage 分区。

流程:staging 目录 → IDF fatfsgen.py --wear-leveling 生成镜像 → esptool write_flash。

注意:烧录会覆盖整个 storage 分区(含设备端 /storage/captures 采集图像)。
训练数据请优先走 PC 端采集归档(python pc/inspector_pc.py)。

前置:
  1) ESP-IDF ≥5.1(取 components/fatfs/fatfsgen.py,支持 --wear-leveling)
  2) pip install esptool

用法:
  python tools/flash_model.py --port COM5
  python tools/flash_model.py --image-only          # 只生成 storage.img 不烧录
  python tools/flash_model.py --port COM5 --skip-labels   # 只烧模型
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PART_OFFSET = 0x320000
PART_SIZE = 0x400000


def find_fatfsgen(explicit: str | None) -> Path:
    """定位 IDF 的 fatfsgen.py:显式路径 → IDF_PATH → 常见安装位置。"""
    cands = []
    if explicit:
        cands.append(Path(explicit))
    if os.environ.get("IDF_PATH"):
        cands.append(Path(os.environ["IDF_PATH"]) / "components" / "fatfs" / "fatfsgen.py")
    cands += [
        Path.home() / "esp" / "esp-idf" / "components" / "fatfs" / "fatfsgen.py",
        Path("C:/Espressif/frameworks/esp-idf-v5.3.1/components/fatfs/fatfsgen.py"),
        Path("C:/Espressif/frameworks/esp-idf-v5.2.2/components/fatfs/fatfsgen.py"),
    ]
    for c in cands:
        if c.is_file():
            return c
    print("找不到 fatfsgen.py。请设置 IDF_PATH 或用 --fatfsgen 指定路径,例如:\n"
          "  --fatfsgen %USERPROFILE%\\esp\\esp-idf\\components\\fatfs\\fatfsgen.py",
          file=sys.stderr)
    sys.exit(1)


def probe_flags(fgs: Path) -> dict[str, str]:
    """fatfsgen.py 参数名各 IDF 版本下划线/横杠不一,从 --help 探测。"""
    help_text = subprocess.run([sys.executable, str(fgs), "--help"],
                               capture_output=True, text=True).stdout
    def pick(*cands: str) -> str:
        return next((c for c in cands if c in help_text), cands[0])
    return {
        "output": pick("--output_file", "--output-file", "-o"),
        "wl": pick("--wear-leveling", "--wear_leveling", "--wl", "-w"),
        "size": pick("--partition-size", "--partition_size"),
        "sector": pick("--sector-size", "--sector_size"),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="打包并烧录边缘模型到 storage 分区")
    ap.add_argument("--model", default=str(REPO / "pc" / "export" / "model_int8.tflite"))
    ap.add_argument("--labels", default=str(REPO / "pc" / "export" / "labels.txt"))
    ap.add_argument("--skip-labels", action="store_true", help="不打包 labels.txt(固件有内置默认)")
    ap.add_argument("--offset", type=lambda s: int(s, 0), default=PART_OFFSET)
    ap.add_argument("--size", type=lambda s: int(s, 0), default=PART_SIZE)
    ap.add_argument("--port", help="串口(烧录必填,如 COM5)")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--fatfsgen", help="fatfsgen.py 路径(默认从 IDF_PATH 找)")
    ap.add_argument("--image-only", action="store_true", help="只生成镜像不烧录")
    ap.add_argument("--image-out", default=str(REPO / "pc" / "export" / "storage.img"))
    args = ap.parse_args()

    model = Path(args.model)
    if not model.is_file():
        print(f"模型不存在:{model}(先运行 python pc/train_edge.py)", file=sys.stderr)
        return 1
    labels = Path(args.labels)
    if not args.skip_labels and not labels.is_file():
        print(f"标签不存在:{labels}", file=sys.stderr)
        return 1
    if not args.image_only and not args.port:
        print("烧录需要 --port(或用 --image-only 只生成镜像)", file=sys.stderr)
        return 1

    fgs = find_fatfsgen(args.fatfsgen)
    print(f"fatfsgen: {fgs}")

    # staging 目录(FATFS 根 = /storage)
    staging = Path(tempfile.mkdtemp(prefix="pcb_model_"))
    try:
        shutil.copy(model, staging / "model_int8.tflite")
        if not args.skip_labels:
            shutil.copy(labels, staging / "labels.txt")

        out = Path(args.image_out)
        fl = probe_flags(fgs)
        cmd = [sys.executable, str(fgs), str(staging),
               fl["output"], str(out),
               fl["wl"],
               fl["sector"], "4096",
               fl["size"], hex(args.size)]
        print(" ".join(cmd))
        r = subprocess.run(cmd)
        if r.returncode != 0 or not out.is_file():
            print("fatfsgen 失败。若为参数不识别,请确认 IDF ≥5.1(旧版 fatfsgen 不支持 --wear-leveling)",
                  file=sys.stderr)
            return 1
        print(f"镜像已生成:{out}({out.stat().st_size / 1024:.0f} KB)")

        if args.image_only:
            return 0

        # esptool v5 用 write-flash,v4 用 write_flash:先试新再退旧
        for sub in ("write-flash", "write_flash"):
            cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3",
                   "-p", args.port, "-b", str(args.baud), sub, hex(args.offset), str(out)]
            r = subprocess.run(cmd)
            if r.returncode == 0:
                print("烧录完成。设备重启后自动加载新模型。")
                return 0
        print("esptool 烧录失败(检查串口/接线/占用)", file=sys.stderr)
        return 1
    finally:
        shutil.rmtree(staging, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
