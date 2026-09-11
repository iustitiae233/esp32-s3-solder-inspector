#!/usr/bin/env python3
"""训练边缘分类模型(MobileNetV2 α0.35, 96x96)并导出 int8 全量化 TFLite。

输入:pc/dataset/<类别名>/*.png|jpg|bmp(PC 端采集归档的原始帧)
输出:pc/export/model_int8.tflite + labels.txt + 混淆矩阵(confusion.txt)

与固件(edge_ai.c)对齐的关键点:
- 输入 96x96x3 int8,量化预处理 x/127.5-1(representative 集同分布)
- 固件读取模型张量自身的 scale/zero_point,任何 affine 参数都兼容

用法:
  python train_edge.py                 # 默认 dataset/ → export/,60 epochs
  python train_edge.py --epochs 100 --alpha 0.35
"""

from __future__ import annotations

import argparse
import random
import sys
from pathlib import Path

import cv2
import numpy as np

HERE = Path(__file__).resolve().parent
IMG_SIZE = 96


def load_labels(dataset_dir: Path) -> list[str]:
    """类别 = labelmap.txt 与 dataset 目录的交集(保持 labelmap 顺序)。"""
    lm = HERE / "labelmap.txt"
    names = [l.strip() for l in lm.read_text(encoding="utf-8").splitlines() if l.strip()] \
        if lm.exists() else []
    dirs = {d.name for d in dataset_dir.iterdir() if d.is_dir()}
    used = [n for n in names if n in dirs]
    used += sorted(dirs - set(used))    # labelmap 外的目录追加在后
    return used


def imread_unicode(path: Path):
    """cv2.imread 在 Windows 不认中文路径,用 imdecode(np.fromfile) 读。"""
    data = np.fromfile(str(path), dtype=np.uint8)
    return cv2.imdecode(data, cv2.IMREAD_COLOR)


def load_images(dataset_dir: Path, labels: list[str]):
    xs, ys = [], []
    exts = ("*.png", "*.jpg", "*.jpeg", "*.bmp")
    for idx, name in enumerate(labels):
        files = []
        for e in exts:
            files += dataset_dir.joinpath(name).glob(e)
        n = 0
        for f in sorted(files):
            bgr = imread_unicode(f)
            if bgr is None:
                continue
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
            rgb = cv2.resize(rgb, (IMG_SIZE, IMG_SIZE), interpolation=cv2.INTER_AREA)
            xs.append(rgb)
            ys.append(idx)
            n += 1
        print(f"  {name}: {n} 张")
    return np.asarray(xs, np.uint8), np.asarray(ys, np.int64)


def main() -> int:
    ap = argparse.ArgumentParser(description="训练边缘 int8 分类模型")
    ap.add_argument("--data", default=str(HERE / "dataset"), help="数据集根目录")
    ap.add_argument("--out", default=str(HERE / "export"), help="输出目录")
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch", type=int, default=32)
    ap.add_argument("--alpha", type=float, default=0.35, help="MobileNetV2 宽度因子")
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--rep", type=int, default=200, help="量化 representative 样本数")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    random.seed(args.seed)
    np.random.seed(args.seed)

    data_dir = Path(args.data)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    if not data_dir.is_dir():
        print(f"数据集不存在:{data_dir}(先用 PC 端采集归档)", file=sys.stderr)
        return 1

    labels = load_labels(data_dir)
    if len(labels) < 2:
        print(f"类别不足 2 个(当前:{labels}),无法训练", file=sys.stderr)
        return 1
    print(f"类别({len(labels)}):{labels}")

    print("加载图像…")
    x, y = load_images(data_dir, labels)
    total = len(x)
    if total < 20:
        print(f"样本过少({total} 张),至少 20 张再训练", file=sys.stderr)
        return 1
    counts = np.bincount(y, minlength=len(labels))
    if (counts < 10).any():
        print(f"警告:有类别少于 10 张 {dict(zip(labels, counts.tolist()))}")

    # 打乱 + 85/15 切分
    idx = np.random.permutation(total)
    x, y = x[idx], y[idx]
    n_val = max(4, int(total * 0.15))
    xv, yv = x[:n_val], y[:n_val]
    xt, yt = x[n_val:], y[n_val:]
    print(f"训练 {len(xt)} / 验证 {len(xv)}")

    # TensorFlow 延迟导入(报错更友好)
    try:
        import tensorflow as tf
    except ImportError:
        print("未安装 TensorFlow:pip install tensorflow", file=sys.stderr)
        return 1
    tf.random.set_seed(args.seed)

    # 归一化 x/127.5-1(与固件量化一致)
    norm = lambda t: (tf.cast(t, tf.float32) / 127.5 - 1.0)
    ds_tr = (tf.data.Dataset.from_tensor_slices((xt, yt))
             .map(lambda im, lb: (norm(im), lb), num_parallel_calls=tf.data.AUTOTUNE)
             .shuffle(min(len(xt), 2048)).batch(args.batch).prefetch(tf.data.AUTOTUNE))
    ds_val = (tf.data.Dataset.from_tensor_slices((xv, yv))
              .map(lambda im, lb: (norm(im), lb)).batch(args.batch))

    base = tf.keras.applications.MobileNetV2(
        input_shape=(IMG_SIZE, IMG_SIZE, 3), alpha=args.alpha,
        include_top=False, weights=None, pooling="avg")
    out = tf.keras.layers.Dense(len(labels))(base.output)   # linear logits
    model = tf.keras.Model(base.input, out)
    model.compile(optimizer=tf.keras.optimizers.Adam(args.lr),
                  loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
                  metrics=["accuracy"])

    cbs = [
        tf.keras.callbacks.EarlyStopping(patience=10, restore_best_weights=True,
                                         monitor="val_accuracy", mode="max"),
        tf.keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=4, min_lr=1e-5),
    ]
    model.fit(ds_tr, validation_data=ds_val, epochs=args.epochs, callbacks=cbs, verbose=2)

    # ---- int8 全量化 ----
    rep_imgs = xt[random.sample(range(len(xt)), min(args.rep, len(xt)))]
    rep_imgs = (rep_imgs.astype(np.float32) / 127.5 - 1.0)

    def representative():
        # 极端样本锁定 [-1,1] 满量程:保证输入 scale/zp 覆盖固件量化公式全程,
        # 避免窄分布数据导致两端饱和(全黑 → -1,全白 → +1)
        yield [np.full((1, IMG_SIZE, IMG_SIZE, 3), -1.0, np.float32)]
        yield [np.full((1, IMG_SIZE, IMG_SIZE, 3), 1.0, np.float32)]
        for im in rep_imgs:
            yield [im[None, ...]]

    conv = tf.lite.TFLiteConverter.from_keras_model(model)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = representative
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    tfl = conv.convert()
    (out_dir / "model_int8.tflite").write_bytes(tfl)
    (out_dir / "labels.txt").write_text("\n".join(labels) + "\n", encoding="utf-8")
    print(f"已导出 {out_dir/'model_int8.tflite'}({len(tfl)/1024:.1f} KB)")

    # ---- 用 TFLite 解释器在验证集上评估 + 混淆矩阵 ----
    interp = tf.lite.Interpreter(model_content=tfl)
    interp.allocate_tensors()
    inp = interp.get_input_details()[0]
    scale, zp = inp["quantization"]

    cm = np.zeros((len(labels), len(labels)), dtype=np.int64)
    for i in range(len(xv)):
        xn = xv[i].astype(np.float32) / 127.5 - 1.0
        q = np.round(xn / scale + zp).astype(np.int8)
        q = np.clip(q, -128, 127)[None, ...]
        interp.set_tensor(inp["index"], q)
        interp.invoke()
        logits = interp.get_tensor(interp.get_output_details()[0]["index"])[0]
        pred = int(np.argmax(logits))
        cm[yv[i], pred] += 1

    lines = ["混淆矩阵(行=真实,列=预测)"]
    lines.append(f"{'':10s}" + "".join(f"{n[:6]:>8s}" for n in labels))
    for i, name in enumerate(labels):
        acc = cm[i, i] / max(1, cm[i].sum())
        lines.append(f"{name[:10]:10s}" + "".join(f"{v:8d}" for v in cm[i])
                     + f"   acc={acc:.0%}")
    report = "\n".join(lines)
    (out_dir / "confusion.txt").write_text(report + "\n", encoding="utf-8")
    print(report)
    print(f"完成。烧录:python tools/flash_model.py --port COMx")
    return 0


if __name__ == "__main__":
    sys.exit(main())
