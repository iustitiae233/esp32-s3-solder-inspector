#ifndef BSP_TOUCH_H
#define BSP_TOUCH_H

#include <stdbool.h>

/**
 * @brief XPT2046 电阻触摸(挂载于 bsp_display 建立的 SPI2 总线,CS=PIN_TP_CS,2MHz)。
 *
 * 必须在 bsp_display_init() 之后调用 bsp_touch_init()。
 * 注册 LVGL pointer indev;原始坐标经 app_nvs 校准矩阵映射到 240x320。
 */
int bsp_touch_init(void);

/** 读取一次原始坐标(12bit)与压力状态。pressed=false 时 x/y 无效。 */
bool bsp_touch_read_raw(int *x_raw, int *y_raw);

#endif /* BSP_TOUCH_H */
