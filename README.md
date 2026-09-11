# 焊点检测仪载板项目

当前设计基线见：[载板设计文档](./2026-09-06-solder-ai-carrier-pcb.md)。

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
