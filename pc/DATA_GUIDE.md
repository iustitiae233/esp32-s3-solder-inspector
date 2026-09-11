# 数据采集与标注指南(LabelImg + YOLO 格式)

从零构建焊点缺陷检测数据集的完整操作手册。边缘分类模型与 YOLO 检测模型共用同一批采集图像,标注方式不同。

## 0. 类别体系(顺序固定,勿改)

| id | 类别 | 说明 |
|---|---|---|
| 0 | OK | 合格焊点(润湿良好、锡量适中) |
| 1 | 多锡 | 锡量过多(馒头状/连边缘) |
| 2 | 少锡 | 锡量不足(引脚轮廓可见/干瘪) |
| 3 | 连锡 | 相邻焊点/引脚桥接短路 |
| 4 | 偏移 | 焊点相对焊盘偏出 |
| 5 | 虚焊 | 表面灰暗粗糙/润湿不良 |

顺序 = `pc/labelmap.txt` = 固件内置默认标签,三处必须一致。

## 1. 采集

1. 设备与 PC 连接后,`inspector_pc.py` → 「开始取流」。
2. 构图建议:
   - **边缘分类用**:单焊点尽量居中、占画面 1/3~2/3(96x96 裁切后仍可辨识);
   - **YOLO 检测用**:整排/整板焊点入镜,每帧 3~10 个焊点。
3. 每帧点「保存当前帧」前先在类别下拉框选对类别(分类采集);或切到通用目录再后续标注(检测采集)。
4. 数量目标:每类 ≥200 张起步,各类尽量均衡;覆盖不同光照/角度/板色。
5. 数据落盘 `pc/dataset/<类别>/`,为 PNG 原始分辨率 240x240。

> 直接文件夹拷贝/重命名也可,目录名必须与类别名完全一致。

## 2. YOLO 标注(LabelImg)

安装:`pip install labelimg`(或 labelImg/labelImg 均可),运行 `labelimg`。

1. 「Open Dir」选 `pc/dataset/<各类别>` 汇总的图像(可先把全部图像软链/复制到 `pc/dataset_yolo/images/all`);
2. 「Change Save Dir」设为同目录(YOLO txt 与图像同目录,之后再按 train/val 分拣);
3. 左侧格式按钮切成 **YOLO**(默认 PascalVOC,务必切换);
4. 快捷键:`W` 画框 → 框住整个焊点(含少量边缘)→ 选类别 → `Ctrl+S` 保存 → `D` 下一张;
5. 一个焊点一个框;连锡按桥接区域整体一个框、类别选"连锡";
6. 类别文件 `classes.txt` 若自动生成,请核对内容顺序与上表一致(不一致也没关系,训练用
   `pc/labelmap.txt` 重新映射,但建议保持一致避免混乱)。

### 分拣 train/val

标注完成后整理为 YOLO 目录结构(85/15 划分示例脚本):

```
pc/dataset_yolo/
  images/train/*.png   images/val/*.png
  labels/train/*.txt   labels/val/*.txt    # 与图像同名
```

```bash
# Windows Git Bash / Linux
cd pc/dataset_yolo && mkdir -p images/train images/val labels/train labels/val
i=0; for f in images/all/*.png; do
  d=val; [ $((i % 7)) -ne 0 ] && d=train
  cp "$f" "images/$d/"; [ -f "labels/all/$(basename "${f%.png}").txt" ] && \
    cp "labels/all/$(basename "${f%.png}").txt" "labels/$d/"
  i=$((i+1)); done
```

生成 data.yaml 并训练:

```bash
python pc/train_yolo.py --make-yaml
python pc/train_yolo.py --epochs 80            # yolov8n, imgsz 240
python pc/train_yolo.py --model yolov8s.pt     # 精度不够时换 s
```

产物 `pc/dataset_yolo/runs/solder_det/weights/best.pt` → 在 `inspector_pc.py` 里「加载模型(.pt)」。

## 3. 边缘分类(无需标注)

`pc/dataset/<类别>/` 目录名即标签,直接:

```bash
python pc/train_edge.py                          # → pc/export/model_int8.tflite
python tools/flash_model.py --port COM5          # 烧录到设备
```

混淆矩阵见 `pc/export/confusion.txt`,某类准确率低 → 补该类样本再训。

## 4. 质量检查清单

- [ ] 各类 ≥200 张,且均衡(最大/最小类样本比 < 3:1)
- [ ] 边缘分类图:焊点居中、大小一致性好
- [ ] YOLO 标注:框紧贴焊点,无漏标、无误选类别
- [ ] classes.txt / labelmap.txt 与上表 6 类顺序一致
- [ ] train/val 无同一焊点的重复帧(数据泄漏会虚高)
