#ifndef BSP_KEY_H
#define BSP_KEY_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_KEY_K1 1   /* GPIO14 */
#define BSP_KEY_K2 2   /* GPIO15 */

#define BSP_KEY_EVENT_PRESS      0   /* 短按(释放时判定) */
#define BSP_KEY_EVENT_LONG_PRESS 1   /* 长按 ≥800ms */

/**
 * @brief K1/K2 按键初始化(内部上拉 + 20ms 软件消抖 + 800ms 长按)。
 * 回调运行在 key 任务上下文(非 ISR),更新 UI 前自行加 LVGL 锁。
 */
void bsp_key_init(void);

/** 注册按键事件回调(仅一个)。 */
void bsp_key_set_cb(void (*on_key)(int key, int event));

/** 当前是否按下(自检用)。 */
bool bsp_key_is_pressed(int key);

#endif /* BSP_KEY_H */
