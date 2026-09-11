# 焊点检测仪载板项目

当前设计基线见：[载板设计文档](./2026-09-06-solder-ai-carrier-pcb.md)。

软件设计见 [docs/superpowers/specs/2026-09-11-solder-defect-inspector-software-design.md](./docs/superpowers/specs/)、实施计划见 [docs/superpowers/plans/](./docs/superpowers/plans/)。

## 目录说明

- `2026-09-06-solder-ai-carrier-pcb.md`：当前有效的电气方案、接口表、BOM、PCB 规则和验收项。
- `YD-ESP32-S3资料/`：核心板原厂资料。
- `OV2640摄像头模块资料（新版本）/`：ATK-MC2640/OV2640 原厂资料。
- `显示屏大小MSP2807_Size.pdf`、`显示屏原理图MSP2807-2.8-SPI.pdf`、
  `tft显示屏2.8inch_SPI_Module_MSP2807_User_Manual_CN.pdf`：显示屏资料。
- `引脚图（已完善）.PNG`：早期引脚分配示意，仅作辅助；最终连接以当前设计文档和嘉立创 EDA 工程为准。
- `archive/auto-generated-schematic/`：早期自动生成的原理图草稿，不是最终嘉立创 EDA 工程，不用于制板。
- `docs/reference-renders/`：从规格书渲染出的核对图片和格式资料，不属于生产文件。

## 当前版本摘要

- 核心板：YD-ESP32-S3-N16R8，2 条 1x22P 排母插装；
- 显示屏：MSP2807，1x14P SPI + XPT2046 触摸；
- 摄像头：ATK-MC2640，2x9P、18 针、自带 24MHz 晶振；
- 照明：独立外部光源，载板无灯环电路；
- GPIO：使用 24 个，GPIO15 接第二个按键 K2，GPIO48 不外接；
- 电容：C1/C2 分别为 K1/K2 消抖；C3/C4/C5 分别用于核心板、屏幕和相机 3.3V 去耦。

## 生产文件提醒

嘉立创 EDA 云端/本地最终工程及其 Gerber、BOM、坐标文件尚未出现在本目录中。
下单前应导出并保存到项目目录，同时保留 ERC、DRC 为零错误的检查记录。

---

# 软件(焊点缺陷检测)

固件(`firmware/`,ESP-IDF + LVGL9)+ PC 检测台(`pc/`,PySide6 + YOLOv8)+ 工具链(`tools/`)。
设备两种检测模式:**边缘分类**(TFLite Micro int8,芯片上推理)与 **PC 检测**(WiFi 推流到电脑 YOLOv8 画框回传)。

## 目录结构

```
firmware/            ESP-IDF 工程(YD-ESP32-S3-N16R8)
  main/bsp/          显示 ILI9341 / 触摸 XPT2046 / 相机 OV2640 / 按键 / 背光
  main/ui/           LVGL 中文界面:主页/检测/采集/统计/设置 + 触摸校准
  main/app_edge_ai/  TFLM int8 推理(96x96 分类)
  main/app_net/      WiFi STA + TCP 协议(proto.h)
  main/app_camera/   帧分发器(发布-订阅)
  main/app_store/    NVS 配置/统计
pc/                  PC 端
  inspector_pc.py    检测台 GUI(TCP server + YOLOv8 + 采集归档)
  proto.py           协议实现(与 proto.h 严格对应)
  train_edge.py      边缘模型训练 + int8 全量化导出
  train_yolo.py      YOLOv8 检测模型训练
  labelmap.txt       类别顺序(OK/多锡/少锡/连锡/偏移/虚焊)
  tests/             协议单元测试
tools/
  flash_model.py     模型打包 FATFS + 烧录 storage 分区
```

## 固件:编译与烧录

需要 [ESP-IDF ≥5.2](https://docs.espressif.com/projects/esp-idf/)(含 Python 3.9+):

```bash
cd firmware
idf.py set-target esp32s3     # 首次
idf.py build
idf.py -p COM5 flash monitor  # Windows;Linux 用 /dev/ttyUSBx
```

- 分区表:`factory` 3MB 应用 + `storage` 4MB FATFS(模型/标签/设备端采集)
- 无模型也能启动:界面显示"模型:未加载",边缘模式禁用,PC 模式不受影响

**首配**:开机 → 设置页配 WiFi(与电脑同一局域网)与电脑 IP(端口默认 3333)→ 触摸校准(可选)。

## PC 端:运行

```bash
cd pc
pip install -r requirements.txt      # 含 pyside6/ultralytics/tensorflow/esptool
python inspector_pc.py               # 默认监听 :3333
python inspector_pc.py --port 3334
```

操作顺序:① 连接区等设备上线 → ②「加载模型(.pt)」选 YOLO 权重 → ③「开始取流」(设备自动切 PC 模式并推流)→ 检测框回传设备屏幕同步显示。也可在设备端「检测页」选 PC 模式。
④ 采集区选类别 →「保存当前帧」归档到 `pc/dataset/<类别>/`(训练数据就来自这里)。

单元测试:`python -m pytest pc/tests/ -q`

## 数据全流程(从零到芯片上跑模型)

1. **采集**:设备对准焊点,PC 端归档(每类建议 ≥200 张,覆盖不同焊点/光照/角度)
2. **边缘分类模型**(整图分类,单焊点居中拍摄):
   ```bash
   python pc/train_edge.py                    # 60 epochs → pc/export/model_int8.tflite
   python tools/flash_model.py --port COM5    # 打包 FATFS 烧到 0x320000
   ```
   重启后自动加载,设备检测页「边缘」模式即用。
3. **YOLO 检测模型**(多焊点画框):用 LabelImg 按 YOLO 格式标注,见
   [pc/DATA_GUIDE.md](./pc/DATA_GUIDE.md) → `python pc/train_yolo.py --make-yaml && python pc/train_yolo.py`

> 注意:`flash_model.py` 烧录会覆盖整个 storage 分区(含设备端 `/storage/captures`)。训练数据以 PC 端归档为准。

## 通信协议(小端)

帧:`[u32 magic 0xA55A1234][u16 type][u16 len][payload]`,PC 为 TCP server(:3333),设备为客户端。

| type | 值 | payload |
|---|---|---|
| HELLO | 0x01 | u8 ver, u8 rsv[3], char name[16], u32 fw_ver |
| IMAGE | 0x02 | u16 w, u16 h, u8 fmt(0=RGB565LE), u8 rsv, u32 frame_id, 像素 |
| DETECT | 0x03 | u32 frame_id, u16 n, ×n{f32 x,y,w,h(240 系), u8 cls, u8 rsv, f32 conf}(每框 22B) |
| HEARTBEAT | 0x04 | u32 时间戳(2s 间隔) |
| COMMAND | 0x05 | u8 cmd(0x01 取流开/0x02 停/0x03 单次), u8 arg |

实现:`firmware/main/app_net/proto.h` ↔ `pc/proto.py`(修改必须两侧同步)。

## 故障排除

- **相机初始化失败**:检查 2x9P 排线方向与供电;模块自带晶振,固件不输出 XCLK。
- **花屏**:确认 `CONFIG_LV_COLOR_16_SW_SWAP=y`(sdkconfig.defaults 已带)。
- **触摸偏**:设置页 → 触摸校准(左上/右下两点)。
- **PC 连不上**:设备与电脑同一局域网、电脑防火墙放行 3333;设备端设置页核对 IP。
- **模型不加载**:`ls /storage`(monitor 里)确认 model_int8.tflite 存在;重新 `flash_model.py`。
