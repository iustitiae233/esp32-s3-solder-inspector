#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_ai.h"
#include "frame_hub.h"
#include "lvgl.h"

/* 页面 */
#define UI_PAGE_HOME     0
#define UI_PAGE_DETECT   1
#define UI_PAGE_CAPTURE  2
#define UI_PAGE_STATS    3
#define UI_PAGE_SETTINGS 4

/* 检测模式 */
typedef enum {
    UI_MODE_PREVIEW = 0,   /* 仅预览 */
    UI_MODE_EDGE,          /* 边缘分类 */
    UI_MODE_PC,            /* PC YOLO 检测 */
} ui_mode_t;

/** 初始化主题并构建全部页面(显示主页)。需在 bsp_display_init/bsp_touch_init 之后。 */
void ui_init(void);

/** 切换页面(线程安全:内部加 LVGL 锁)。 */
void ui_switch_page(int page_id);

int ui_current_page(void);

/** 当前检测模式。 */
ui_mode_t ui_get_mode(void);

/** 切换模式(同步 frame_hub 掩码)。 */
void ui_set_mode(ui_mode_t m);

/** 连续检测开关(EDGE/PC 模式下生效)。 */
void ui_set_continuous(bool on);
bool ui_get_continuous(void);

/** 请求一次单次检测(当前模式为 EDGE/PC 时有效)。 */
void ui_request_single_shot(void);

/** 相机任务消费:是否有挂起的单次检测请求(原子读取并清除)。 */
bool ui_take_single_shot_edge(void);
bool ui_take_single_shot_net(void);

/** 相机任务订阅回调:更新预览图像(内部拷贝到私有缓冲后加锁更新)。 */
void ui_set_preview_frame(const frame_t *f);

/** 边缘推理结果 → 检测页(线程安全)。 */
void ui_show_edge_result(const edge_result_t *r);

/** PC 检测结果 payload(DETECT 格式)→ 检测页画框(线程安全)。 */
void ui_show_pc_boxes(const uint8_t *payload, int len);

/** 刷新状态显示(主页状态卡/检测页状态条)。 */
void ui_refresh_status(void);

/** K1/K2 事件入口(bsp_key 回调转发到这里)。 */
void ui_on_key(int key, int event);

/** 页面构建时注册预览 image 控件(最多 2 个:检测页/采集页)。 */
void ui_register_preview_img(lv_obj_t *img);

/** 预览图像描述符(data=共享预览缓冲)。 */
const lv_image_dsc_t *ui_preview_dsc(void);

/** 进入触摸校准(设置页入口,覆盖层实现于 ui_calib.c)。 */
void ui_calib_enter(void);

/** 预览区尺寸(检测页/采集页共用)。 */
#define UI_PREVIEW_W 240
#define UI_PREVIEW_H 240

#endif /* UI_H */
