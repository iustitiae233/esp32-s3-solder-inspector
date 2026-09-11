#ifndef FRAME_HUB_H
#define FRAME_HUB_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** 订阅位:按检测模式组合(frame_hub_set_mask)。 */
#define FH_SUB_DISPLAY (1U << 0)
#define FH_SUB_EDGE_AI (1U << 1)
#define FH_SUB_NET     (1U << 2)
#define FH_SUB_ALL     (FH_SUB_DISPLAY | FH_SUB_EDGE_AI | FH_SUB_NET)

/** 一帧相机数据(RGB565 小端 240x240,数据归相机 fb 所有,回调返回前有效)。 */
typedef struct {
    uint8_t   *buf;
    uint32_t   frame_id;   /* 递增 */
    TickType_t timestamp;
} frame_t;

typedef void (*frame_cb_t)(const frame_t *f);

/** 注册订阅者回调(sub 取 FH_SUB_* 之一;每个订阅位仅一个回调)。 */
void frame_hub_on_frame(uint32_t sub, frame_cb_t cb);

/** 设置当前订阅掩码(模式切换核心,原子写)。 */
void frame_hub_set_mask(uint32_t mask);

/** 当前订阅掩码。 */
uint32_t frame_hub_get_mask(void);

/** 相机任务调用:按掩码同步顺序回调各订阅者(运行在相机任务上下文,禁止阻塞)。 */
void frame_hub_dispatch(const frame_t *f);

#endif /* FRAME_HUB_H */
