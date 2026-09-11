/**
 * @file bsp_pins.h
 * @brief 全部 GPIO 分配 —— 严格对应《载板设计文档 v1.2》§3.1,禁止在别处出现裸 GPIO 编号。
 *
 * 硬件要点:
 *  - 相机 ATK-MC2640 自带 24MHz 晶振 → XCLK 不连接(xclk_pin = -1);
 *    PWDN 接 GND、RST 经 10k 上拉、FLASH 经 10k 下拉,均为硬件固定电平,固件不控制。
 *  - LCD 与触摸共享 SPI2 总线:SCLK=GPIO40、MOSI=GPIO41(T_DIN 同网);T_DO 为独立 MISO。
 *  - 按键低电平有效,内部上拉 + 100nF 硬件消抖 + 软件消抖。
 *  - GPIO35/36/37 为 N16R8 模组 Octal PSRAM 内部占用;GPIO0/3/45/46 为 strapping,不外接。
 */
#ifndef BSP_PINS_H
#define BSP_PINS_H

/* ---------------- 摄像头 ATK-MC2640(OV2640, 18 针) ---------------- */
#define PIN_CAM_D0        11
#define PIN_CAM_D1         9
#define PIN_CAM_D2         8
#define PIN_CAM_D3        10
#define PIN_CAM_D4        12
#define PIN_CAM_D5        18
#define PIN_CAM_D6        17
#define PIN_CAM_D7        16
#define PIN_CAM_VSYNC      6
#define PIN_CAM_HREF       7
#define PIN_CAM_PCLK      13
#define PIN_CAM_SDA        4   /* SCCB 数据,R4 4.7k 上拉 */
#define PIN_CAM_SCL        5   /* SCCB 时钟,R5 4.7k 上拉 */

/* ---------------- 屏幕 MSP2807(ILI9341, SPI) ---------------- */
#define PIN_LCD_SCLK      40   /* 与 T_CLK 共网 */
#define PIN_LCD_MOSI      41   /* 与 T_DIN 共网 */
#define PIN_LCD_CS        42
#define PIN_LCD_DC        38
#define PIN_LCD_RST       39
#define PIN_LCD_BL        21   /* 模块板载 S8050 驱动背光 */

/* ---------------- 触摸 XPT2046(电阻式, 共享 SPI2) ---------------- */
#define PIN_TP_MISO        1   /* T_DO */
#define PIN_TP_CS          2   /* R1 10k 上拉 */
#define PIN_TP_IRQ        47   /* 低有效 */

/* ---------------- 按键(低电平有效, 内部上拉) ---------------- */
#define PIN_KEY_K1        14
#define PIN_KEY_K2        15   /* 原相机 XCLK 引脚,载板 v1.1 起改接 K2 */

/* ---------------- 总线与分辨率参数 ---------------- */
#define LCD_SPI_HOST      SPI2_HOST
#define LCD_SPI_FREQ_HZ   (40 * 1000 * 1000)  /* ILI9341 SPI,实测 MSP2807 可稳定运行 */
#define TP_SPI_FREQ_HZ    (2 * 1000 * 1000)   /* XPT2046 规格 ≤ 2.5MHz */

#define LCD_HRES          240
#define LCD_VRES          320

#define CAM_HRES          240
#define CAM_VRES          240

#endif /* BSP_PINS_H */
