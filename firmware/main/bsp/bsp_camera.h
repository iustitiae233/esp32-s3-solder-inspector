#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#include <stdbool.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#if ESP_IDF_VERSION_MAJOR >= 5
#include "esp_camera.h"
#else
#include "esp_camera.h"
#endif

/**
 * @brief ATK-MC2640(OV2640)初始化。
 *
 * 模块自带 24MHz 有源晶振 → pin_xclk = -1(xclk_freq_hz=0)。
 * PWDN/RST/FLASH 均为载板硬件固定电平,固件不控制。
 * 输出 RGB565 小端 240x240,双帧缓冲于 PSRAM,grab_latest 模式。
 *
 * @return 0 成功;-1 失败(SCCB 不通/无帧等,日志含原因)
 */
int bsp_camera_init(void);

/** 阻塞取一帧(使用完必须 fb_return)。wait=0 立即返回,可能为 NULL。 */
camera_fb_t *bsp_camera_fb_get(TickType_t wait);

/** 归还帧缓冲给驱动。 */
void bsp_camera_fb_return(camera_fb_t *fb);

#endif /* BSP_CAMERA_H */
