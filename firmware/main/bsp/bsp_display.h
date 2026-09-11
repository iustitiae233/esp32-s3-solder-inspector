#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include "lvgl.h"

/**
 * @brief 初始化 SPI2 总线 + ILI9341 + LVGL 9 显示端口 + LVGL 任务。
 *
 * 总线同时挂载触摸设备(bsp_touch.c),MISO 使用 PIN_TP_MISO。
 * LVGL 渲染缓冲:双 40 行 RGB565(DMA 内存),partial 模式。
 * CONFIG_LV_COLOR_16_SW_SWAP=y 保证 SPI 输出字节序符合 ILI9341。
 *
 * @return 0 成功;-1 失败(日志含原因)
 */
int bsp_display_init(void);

/** LVGL 互斥锁(相机/网络等外部任务更新 UI 前必须持有)。 */
void bsp_display_lvgl_lock(void);

/** 释放 LVGL 互斥锁。 */
void bsp_display_lvgl_unlock(void);

/** 取 LVGL display 对象(仅持锁时访问)。 */
lv_display_t *bsp_display_lvgl_disp(void);

#endif /* BSP_DISPLAY_H */
