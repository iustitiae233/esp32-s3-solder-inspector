#ifndef BSP_BACKLIGHT_H
#define BSP_BACKLIGHT_H

#include <stdint.h>

/** 初始化背光 PWM(LEDC, 5kHz, GPIO21, 模块板载 S8050 高电平点亮)。 */
int bsp_backlight_init(void);

/** 设置亮度百分比 0~100,立即生效。 */
void bsp_backlight_set(int percent);

/** 当前亮度百分比。 */
int bsp_backlight_get(void);

#endif /* BSP_BACKLIGHT_H */
