# 焊点缺陷检测仪软件系统设计(spec)

> 日期:2026-09-11
> 状态:待用户审阅
> 上游文档:`2026-09-06-solder-ai-carrier-pcb.md`(载板设计 v1.2,硬件基线)
> 决策记录:固件用 ESP-IDF;边缘 AI 用 TFLite Micro int8;PC 端用 Python+PySide6+YOLOv8;
> 识别形态为"边缘分类 + PC 检测";缺陷体系为标准 6 类;数据从零采集;仓库已初始化 git。

---

## 1. 目标与范围

在 YD-ESP32-S3-N16R8 载板(已完成 PCB 设计)上实现焊点缺陷检测仪的完整软件系统:

1. **固件**(ESP-IDF 5.x + LVGL 9):驱动 ILI9341 触摸屏与 OV2640 摄像头,提供中文操作界面;
2. **边缘 AI**:芯片上运行 int8 量化分类模型,对单个焊点给出缺陷类别;
3. **PC 协同**:通过 WiFi 把图像送到电脑,YOLOv8 全图检测,结果回传屏幕画框;
4. **数据工具链**:从零采集 → 标注 → 训练 → 烧录的完整闭环脚本与文档。

不在范围内:载板硬件改动、OTA 升级、多设备管理、云端服务。

### 成功标准

- 无模型时固件全功能可用(UI 明确提示"未加载模型");
- 烧入模型后,边缘单帧推理 ≤ 500ms(96×96 int8,MobileNetV2-0.35);
- PC 模式端到端延迟(拍摄→屏幕画框)≤ 1s,帧率 ≥ 5fps;
- 从采集到烧录新模型,全过程只需运行 README 中的脚本,不改固件代码。

## 2. 硬件约束(引自载板 v1.2,固件必须严格遵守)

核心板 YD-ESP32-S3-N16R8:16MB Flash、8MB Octal PSRAM(GPIO35/36/37 被模组占用)。

| 功能 | GPIO | 功能 | GPIO |
|---|---:|---|---:|
| CAM_D0 | 11 | LCD_SCLK / T_CLK(共享) | 40 |
| CAM_D1 | 9 | LCD_MOSI / T_DIN(共享) | 41 |
| CAM_D2 | 8 | LCD_CS | 42 |
| CAM_D3 | 10 | LCD_DC | 38 |
| CAM_D4 | 12 | LCD_RST | 39 |
| CAM_D5 | 18 | LCD_BL | 21 |
| CAM_D6 | 17 | TP_MISO / T_DO | 1 |
| CAM_D7 | 16 | TP_CS | 2 |
| CAM_VSYNC | 6 | TP_IRQ | 47 |
| CAM_HREF | 7 | KEY_K1 | 14 |
| CAM_PCLK | 13 | CAM_SDA(I2C/SCCB) | 4 |
| CAM_SCL | 5 | KEY_K2 | 15 |

要点:

- **相机自带 24MHz 晶振,XCLK 不连接** → `xclk_pin = -1`;PWDN 接 GND、RST 经 10k 上拉、FLASH 经 10k 下拉(硬件固定电平,固件不控制);
- LCD 与触摸**共享 SPI 总线**(SCLK/MOSI 共网),T_DO 独立 MISO;LCD SDO 不接(NC);
- 按键低电平有效,内部上拉 + 100nF 硬件消抖 + 软件消抖;
- 无 XCLK 意味着 GPIO15 只作 K2;GPIO48 留给核心板 RGB,不用。

## 3. 系统架构

```
OV2640 ──RGB565 240×240──> 帧分发器(固件) ──┬─> ILI9341 预览(LVGL 9)
                                            ├─> TFLM int8 边缘分类(96×96)
                                            └─> WiFi STA ──TCP──> PC 端(PySide6)
                                                                ├─ YOLOv8 检测 → 结果回传 → 屏幕 bbox
                                                                ├─ 数据采集归档
                                                                └─ 训练脚本(边缘/YOLO)
```

单一帧格式贯穿全系统:**RGB565, 240×240**(屏幕宽 240 零缩放直写;等比缩到 96×96 进模型;115KB/帧经 WiFi 传 PC)。协议预留格式字段,未来可加 JPEG 而不破坏架构。

## 4. 固件设计(ESP-IDF 5.2+)

### 4.1 目录

```
firmware/
├── CMakeLists.txt  sdkconfig.defaults  partitions.csv
└── main/
    ├── idf_component.yml      # lvgl^9.3, espressif/esp32-camera, espressif/esp-tflite-micro
    ├── main.c                 # 启动顺序与自检
    ├── bsp/{bsp_display, bsp_touch, bsp_camera, bsp_key, bsp_backlight}
    ├── app_camera/frame_hub   # 帧分发器
    ├── app_edge_ai/           # TFLM 封装
    ├── app_net/{wifi_sta, pc_link}  # WiFi + TCP 协议
    ├── app_store/app_nvs      # 配置与统计持久化
    └── ui/                    # 主题、页面管理器、5 个页面、校准
```

### 4.2 模块职责

| 模块 | 职责 | 关键接口 |
|---|---|---|
| bsp_display | SPI2 总线(主机)+ ILI9341 设备(40MHz);初始化序列、窗口写、DMA 异步 | `bsp_display_init()`, flush 回调 |
| bsp_touch | XPT2046 设备(2MHz)挂同一总线;12-bit 差分读、压力阈值 | `bsp_touch_read(x,y,pressed)` |
| bsp_camera | esp32-camera 集成,xclk=-1,240×240 RGB565,fb_count=2 放 PSRAM | `app_camera_start()` |
| frame_hub | 帧分发:显示/边缘/网络三个订阅位,按模式组合 | `frame_hub_subscribe(mask, cb)` |
| app_edge_ai | 从 /storage 读 model_int8.tflite + labels.txt;arena 200KB(PSRAM);RGB565→96×96→int8 | `edge_ai_load()`, `edge_ai_classify(frame, &result)` |
| app_net | WiFi STA(NVS 凭据,重连);TCP 客户端(地址来自设置,默认端口 3333);协议编解码 | `pc_link_send_frame()`, 结果回调 |
| app_store | NVS:WiFi 凭据、PC 地址、阈值、背光、校准系数、分类计数 | get/set 各项 |
| ui | LVGL 页面框架与全部交互 | 见 §5 |

### 4.3 任务与内存

- 任务:`lvgl`(优先级 4,30ms 周期 lv_timer_handler)、`cam`(优先值 5,取帧+分发)、`tcp`(优先级 3)、主任务做初始化与自检;
- LVGL 双 40 行渲染缓冲(内部 SRAM ~40KB);相机 fb 与 TFLM arena 放 PSRAM;
- 预估内存:SRAM 占用(WiFi+LVGL+DMA)< 400KB,PSRAM 占用 < 1MB,余量充足。

### 4.4 分区表(16MB Flash)

```
nvs 0xA000 | phy 0x1000 | factory 3MB | storage(FATFS)4MB | 其余保留
```

/storage 根目录:`model_int8.tflite`、`labels.txt`、`captures/`(机内采集图)。换模型只需重新烧 storage 分区,不重编固件。

### 4.5 LVGL 移植要点

- LVGL 9 显示端口:RGB565、双缓冲、`LV_DISPLAY_RENDER_MODE_PARTIAL`;flush 里若区域全宽则单次 DMA,否则逐行;DMA 完成回调中调 `lv_display_flush_ready`;
- 触摸:pointer indev,读 XPT2046 原始值→校准系数(2 点校准存 NVS)→屏幕坐标;压力不足则报 release;
- 总线仲裁:SPI 驱动按事务串行,触摸轮询在帧间隙插入,实测预览时触摸延迟 ≤ 40ms,可接受;
- 中文显示:启用 LVGL 内置 `lv_font_simsun_16_cjk`(含常用 1000 汉字)作为界面主字体,数字/英文用 Montserrat 混排。

## 5. UI 设计(中文,240×320 竖屏)

全局:深色工业主题、状态栏(IP/连接图标/时间)、底部无实体导航键(页面间用返回按钮+主页入口)。

| 页面 | 内容与交互 |
|---|---|
| 主页 | 标题;状态卡:WiFi/IP、PC 连接、模型加载、帧率;四个入口大按钮:检测/采集/统计/设置 |
| 检测页 | 240×240 预览(直写);模式 segmented:预览/边缘/PC;单次检测按钮+连续开关;结果条:类别+置信度仪表;PC 模式下 bbox 叠加层(LVGL canvas,坐标从 240×240 原图坐标映射);推理耗时显示;K1=触发单次检测,K2=返回 |
| 采集页 | 预览+6 类别单选+保存按钮(存 /storage/captures/<类>/ 或经 PC 面板);剩余空间提示 |
| 统计页 | 各类计数列表+条形图、总数、NG 率、清零按钮(确认弹窗) |
| 设置页 | WiFi SSID/密码(屏上键盘)、PC IP:端口、置信度阈值滑条、背光滑条、触摸校准入口(引导点左上/右下)、恢复默认(确认弹窗) |

模式定义:预览(仅显示)、边缘(显示+每帧/单帧本地推理)、PC(显示+持续推流,收到结果画框)。

## 6. 边缘 AI

- 类别(6):`ok(OK 正常)`、`excess(多锡)`、`insufficient(少锡)`、`bridge(连锡/桥接)`、`misalignment(偏移)`、`cold_joint(虚焊)`。类别表由 labels.txt 决定,固件不写死数量;
- 模型:MobileNetV2 α=0.35,输入 96×96×3 int8(量化后),输出 int8 logits,固件内 softmax;
- 预处理:相机帧中心裁剪 240×240 → 双线性/最近邻缩到 96×96 → RGB565 拆 R/G/B → int8(按模型量化 scale/zero_point);
- 部署:`tools/flash_model.py` 生成 FATFS 镜像并经 esptool 烧到 storage 分区;
- 容错:模型缺失/损坏 → UI 状态"未加载模型",边缘模式按钮禁用,其余功能不受影响。

## 7. 通信协议(TCP,小端)

帧:`[u32 magic=0xA55A1234][u16 type][u16 len][payload]`。

| type | 方向 | payload |
|---|---|---|
| 0x01 HELLO | 双向 | 设备名/版本、能力 |
| 0x02 IMAGE | 设备→PC | u16 w, u16 h, u8 fmt(0=RGB565), u8 reserved, 像素数据 |
| 0x03 DETECT_RESULT | PC→设备 | u32 frame_id, u16 n, ×n{f32 x,y,w,h(原图像素), u8 cls, f32 conf} |
| 0x04 HEARTBEAT | 双向 | 时间戳 |
| 0x05 COMMAND | 双向 | u8 cmd(开始/停止推流、拍照、切模式)+ 参数 |

PC 为 TCP Server(:3333),设备为客户端,断线 3s 重连;frame_id 贯穿"发图→回果"配对。

## 8. PC 端(PySide6)

`pc/inspector_pc.py` 单应用,布局:左侧实时画面+检测框,右侧控制面板。

- 连接区:监听状态、设备 IP、心跳指示;
- 画面区:QImage 显示 RGB565 帧,YOLO 框与类别置信度叠加,NG 框红色/OK 绿色;
- 推理区:模型路径选择(默认 yolov8n.pt,可换自训 best.pt)、置信度阈值、推理开关(关=纯监视);
- 采集区:类别下拉(6 类)+「保存当前帧」+ 快捷键;自动存 `pc/dataset/<class>/yyyymmdd_hhmmss_xyz.png`;计数显示;
- 日志区:连接事件、检测结果流水、简单统计(各类计数)。

`pc/requirements.txt`:pyside6、numpy、ultralytics、opencv-python。

## 9. 训练工具链(从零数据到部署)

1. **采集**:设备采集页 + PC 采集面板,按类归档;分类数据即存即用;检测数据先存图;
2. **标注(仅检测需要)**:LabelImg/YOLO 格式,指南写明 6 类的 class id 顺序与 data.yaml 模板;
3. **train_edge.py**:`dataset/<class>/*.png` → 增强(翻转/亮度/噪声)→ 8:2 划分 → MobileNetV2-0.35(96×96)迁移学习 → int8 全量化(代表集)→ `model_int8.tflite` + `labels.txt` + 测试集准确率/混淆矩阵报告;
4. **train_yolo.py**:`dataset_yolo/`(YOLO 格式)→ yolov8n 训练 → `runs/detect/train/weights/best.pt` → PC 端加载;
5. **flash_model.py**:fatfs 打包 + esptool 烧录 storage 分区;
6. 无 GPU 环境给出 CPU 训练参数建议(小数据集 CPU 可行)。

## 10. 错误处理

- 自检失败(SPI/相机初始化/PSRAM)→ 屏幕红屏错误页+错误码,串口同输出;
- WiFi 连不上 → 主页状态显示,不阻塞本地功能;
- PC 断线 → 检测页状态"PC 未连接",自动重连;
- 协议坏帧 → 丢弃重同步(扫 magic);
- NVS 损坏 → 恢复默认配置。

## 11. 测试与验收

- 固件:BSP 各模块自检函数;`idf.py build` 零错误零警告为合入基线;
- 协议:PC 端内置「回环测试」(自连自发收,校验编解码);
- 端到端验收清单:预览 ≥ 10fps;触摸点击/校准正常;K1/K2 功能正常;边缘推理 ≤ 500ms;PC 模式延迟 ≤ 1s;采集文件正确归档;统计计数正确;断电重启配置与统计保留。

## 12. 风险与对策

| 风险 | 对策 |
|---|---|
| esp32-camera 版本对 xclk=-1 支持差异 | idf_component.yml 锁定已验证版本区间;代码注释说明备选写法 |
| RGB565 推流帧率不足 | 协议已留 fmt 字段,可平滑升级 JPEG;检测场景以单次触发为主,不依赖高帧率 |
| 240×240 帧率上不去(esp32-camera 档位) | 退到 QVGA 320×240+中心裁剪,代码隔离在相机模块一处 |
| 中文 CJK 字体覆盖不全 | 界面文案限制在常用字集;缺失字以英文/图标兜底 |
| 从零数据量小,分类过拟合 | 训练脚本内置强增强+早停;文档建议每类 ≥ 150 张 |

## 13. 交付物清单

固件源码、PC 端源码、训练/烧录脚本、requirements.txt、README(全流程)、spec 与实施计划文档。
