#!/usr/bin/env python3
"""YOLOv8 焊点缺陷检测模型训练(LabelImg YOLO 格式数据)。

数据布局(pc/dataset_yolo/,用 LabelImg 以 YOLO 格式标注 PC 端采集的图):
  dataset_yolo/images/train/*.png   dataset_yolo/images/val/*.png
  dataset_yolo/labels/train/*.txt   dataset_yolo/labels/val/*.txt

用法:
  python train_yolo.py --make-yaml          # 生成/刷新 data.yaml
  python train_yolo.py --epochs 80          # 训练(yolov8n, imgsz 240)
  python train_yolo.py --epochs 80 --model yolov8s.pt
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE / "dataset_yolo"


def make_yaml(root: Path, labels: list[str]) -> Path:
    """生成 data.yaml(类别顺序 = labelmap.txt)。"""
    for sub in ("images/train", "images/val"):
        if not (root / sub).is_dir():
            print(f"缺目录 {root / sub}。请先采集并把图放入 images/,标注放 labels/(YOLO txt)",
                  file=sys.stderr)
            sys.exit(1)
    yml = root / "data.yaml"
    lines = [
        f"path: {root.resolve().as_posix()}",
        "train: images/train",
        "val: images/val",
        f"nc: {len(labels)}",
        "names:",
        *[f"  {i}: {n}" for i, n in enumerate(labels)],
    ]
    yml.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"已生成 {yml}(nc={len(labels)})")
    return yml


def main() -> int:
    ap = argparse.ArgumentParser(description="YOLOv8 焊点缺陷检测训练")
    ap.add_argument("--root", default=str(ROOT), help="数据集根目录")
    ap.add_argument("--make-yaml", action="store_true", help="只生成 data.yaml 不训练")
    ap.add_argument("--model", default="yolov8n.pt", help="预训练权重(n/s/m)")
    ap.add_argument("--epochs", type=int, default=80)
    ap.add_argument("--imgsz", type=int, default=240)
    ap.add_argument("--batch", type=int, default=16)
    args = ap.parse_args()

    root = Path(args.root)
    lm = HERE / "labelmap.txt"
    labels = [l.strip() for l in lm.read_text(encoding="utf-8").splitlines() if l.strip()]

    yml = root / "data.yaml"
    if args.make_yaml or not yml.exists():
        yml = make_yaml(root, labels)
    if args.make_yaml:
        return 0

    try:
        from ultralytics import YOLO
    except ImportError:
        print("未安装 ultralytics:pip install ultralytics", file=sys.stderr)
        return 1

    model = YOLO(args.model)
    model.train(data=str(yml.resolve()), epochs=args.epochs,
                imgsz=args.imgsz, batch=args.batch, patience=20,
                project=str(root / "runs"), name="solder_det")
    best = root / "runs" / "solder_det" / "weights" / "best.pt"
    if best.exists():
        print(f"训练完成:{best}")
        print(f"PC 端加载该模型:inspector_pc.py → 「加载模型(.pt)」→ {best}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
