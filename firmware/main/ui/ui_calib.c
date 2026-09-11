/* 触摸校准:全屏覆盖层,左上/右下两点采样,解 6 参数仿射存 NVS 并热生效 */
#include "lvgl.h"

#include <stdio.h>

#include "app_nvs.h"
#include "bsp_touch.h"
#include "ui_theme.h"

#define N_SAMPLES 8            /* 每点采样的稳定读数个数 */

static lv_obj_t *s_layer;      /* 全屏覆盖层 */
static lv_obj_t *s_hint;       /* 提示文本 */
static lv_obj_t *s_dot;        /* 十字靶(移动到目标点) */
static lv_timer_t *s_timer;

static int s_stage;            /* 0=等第一点按下 1=采第一点 2=等第二点 3=采第二点 4=完成 */
static int s_got;              /* 已采样数 */
static int32_t s_sum_x, s_sum_y;
static int s_raw0[2], s_raw1[2];   /* 两点原始值 */

static const int s_targets[2][2] = { { 12, 12 }, { 228, 308 } };

static void cleanup(void)
{
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = NULL;
    if (s_layer) lv_obj_delete(s_layer);
    s_layer = NULL;
    s_dot = NULL;
    s_hint = NULL;
}

static void save_calib(void)
{
    /* 屏幕点 p0=(12,12) p1=(228,308);原始 r0,r1。
     * 每轴解 scale/offset。XPT2040 典型安装:X_raw 随屏幕 y 变化(电阻膜旋转 90°),
     * 用通用 2x2 线性方程组(以 X 为例):
     *   sx*r0x + kx*r0y + tx = 12
     *   sx*r1x + kx*r1y + tx = 228
     * 两点只能定 2 个未知数 —— 取对角模型(kx=ky=0):
     *   sx = (228-12)/(r1x-r0x), tx = 12 - sx*r0x
     * 载板 XPT2040 与屏幕同向安装(MADCTL=0),对角模型即够用。 */
    float m[6];
    float dx = (float)(s_raw1[0] - s_raw0[0]);
    float dy = (float)(s_raw1[1] - s_raw0[1]);
    if (dx > -1e-3f && dx < 1e-3f) dx = 1.0f;
    if (dy > -1e-3f && dy < 1e-3f) dy = 1.0f;
    m[0] = (float)(s_targets[1][0] - s_targets[0][0]) / dx;   /* x: sx */
    m[1] = 0.0f;
    m[2] = (float)s_targets[0][0] - m[0] * (float)s_raw0[0];  /* x: tx */
    m[3] = 0.0f;
    m[4] = (float)(s_targets[1][1] - s_targets[0][1]) / dy;   /* y: sy */
    m[5] = (float)s_targets[0][1] - m[4] * (float)s_raw0[1];  /* y: ty */
    app_nvs_set_calib(m);
    bsp_touch_update_calib();
}

static void next_stage_ui(void)
{
    if (s_stage <= 1) {
        lv_label_set_text(s_hint, "点击十字中心");
        lv_obj_set_pos(s_dot, s_targets[0][0] - 10, s_targets[0][1] - 10);
    } else if (s_stage <= 3) {
        lv_label_set_text(s_hint, "再点右下角十字");
        lv_obj_set_pos(s_dot, s_targets[1][0] - 10, s_targets[1][1] - 10);
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    int rx, ry;
    bool pressed = bsp_touch_read_raw(&rx, &ry);

    switch (s_stage) {
    case 0:                        /* 等待第一点按下 */
        if (pressed) {
            s_stage = 1;
            s_got = 0;
            s_sum_x = s_sum_y = 0;
        }
        break;
    case 1:                        /* 采样第一点 */
        if (pressed) {
            s_sum_x += rx;
            s_sum_y += ry;
            if (++s_got >= N_SAMPLES) {
                s_raw0[0] = s_sum_x / N_SAMPLES;
                s_raw0[1] = s_sum_y / N_SAMPLES;
                s_stage = 2;
            }
        }
        break;
    case 2:                        /* 等待抬起 → 第二点 */
        if (!pressed) {
            s_stage = 3;
            s_got = 0;
            s_sum_x = s_sum_y = 0;
            next_stage_ui();
        }
        break;
    case 3:                        /* 采样第二点 */
        if (pressed) {
            s_sum_x += rx;
            s_sum_y += ry;
            if (++s_got >= N_SAMPLES) {
                s_raw1[0] = s_sum_x / N_SAMPLES;
                s_raw1[1] = s_sum_y / N_SAMPLES;
                save_calib();
                s_stage = 4;
                lv_label_set_text(s_hint, "校准完成,点击任意处退出");
                lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
            }
        }
        break;
    case 4:                        /* 完成:等抬起后退出 */
        if (!pressed) {
            cleanup();
            return;
        }
        break;
    default:
        break;
    }
}

void ui_calib_enter(void)
{
    if (s_layer != NULL) return;   /* 已在校准中 */

    s_layer = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_layer, 240, 320);
    lv_obj_set_pos(s_layer, 0, 0);
    lv_obj_set_style_bg_color(s_layer, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_layer, 0, 0);
    lv_obj_set_style_radius(s_layer, 0, 0);
    lv_obj_set_style_pad_all(s_layer, 0, 0);
    lv_obj_clear_flag(s_layer, LV_OBJ_FLAG_SCROLLABLE);

    s_hint = lv_label_create(s_layer);
    lv_obj_set_style_text_font(s_hint, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_hint, COL_TEXT, 0);
    lv_label_set_text(s_hint, "点击十字中心");
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 6);

    /* 十字靶:20x20 线条 */
    s_dot = lv_obj_create(s_layer);
    lv_obj_set_size(s_dot, 20, 20);
    lv_obj_set_style_border_color(s_dot, COL_PRIMARY, 0);
    lv_obj_set_style_border_width(s_dot, 2, 0);
    lv_obj_set_style_radius(s_dot, 0, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_dot, 0, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *h1 = lv_obj_create(s_dot);
    lv_obj_set_size(h1, 18, 2);
    lv_obj_align(h1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(h1, COL_PRIMARY, 0);
    lv_obj_set_style_border_width(h1, 0, 0);
    lv_obj_set_style_radius(h1, 0, 0);
    lv_obj_clear_flag(h1, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *v1 = lv_obj_create(s_dot);
    lv_obj_set_size(v1, 2, 18);
    lv_obj_align(v1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(v1, COL_PRIMARY, 0);
    lv_obj_set_style_border_width(v1, 0, 0);
    lv_obj_set_style_radius(v1, 0, 0);
    lv_obj_clear_flag(v1, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_stage = 0;
    s_got = 0;
    s_sum_x = s_sum_y = 0;
    next_stage_ui();
    s_timer = lv_timer_create(timer_cb, 20, NULL);
}
