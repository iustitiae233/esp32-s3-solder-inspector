#include "ui.h"

#include <string.h>

#include "app_nvs.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

#include "ui_theme.h"

static const char *TAG = "ui";

/* 各页面构建/显示钩子(各页面文件提供) */
void ui_page_home_create(lv_obj_t *page);
void ui_page_detect_create(lv_obj_t *page);
void ui_page_capture_create(lv_obj_t *page);
void ui_page_stats_create(lv_obj_t *page);
void ui_page_settings_create(lv_obj_t *page);
void ui_page_home_on_show(void);
void ui_page_detect_on_show(void);
void ui_page_capture_on_show(void);
void ui_page_stats_on_show(void);
void ui_page_settings_on_show(void);
void ui_page_detect_sync_controls(void);
void ui_page_detect_refresh_status(void);
void ui_page_capture_save(void);
void ui_calib_enter(void);

static lv_obj_t *s_pages[5];
static int s_cur = -1;

static volatile ui_mode_t s_mode = UI_MODE_PREVIEW;
static volatile bool s_continuous = false;
static volatile bool s_shot_edge = false;
static volatile bool s_shot_net = false;

static uint8_t *s_preview_buf = NULL;   /* PSRAM,240*240*2,LVGL 渲染期间保持有效 */

/* ---------------- 模式与掩码 ---------------- */

static void apply_mode_mask(void)
{
    uint32_t m = FH_SUB_DISPLAY;
    if (s_mode == UI_MODE_EDGE && s_continuous) m |= FH_SUB_EDGE_AI;
    if (s_mode == UI_MODE_PC && s_continuous) m |= FH_SUB_NET;
    frame_hub_set_mask(m);
}

ui_mode_t ui_get_mode(void) { return s_mode; }

void ui_set_mode(ui_mode_t m)
{
    bsp_display_lvgl_lock();
    s_mode = m;
    bsp_display_lvgl_unlock();
    apply_mode_mask();
    bsp_display_lvgl_lock();
    ui_page_detect_sync_controls();
    bsp_display_lvgl_unlock();
}

void ui_set_continuous(bool on)
{
    s_continuous = on;
    apply_mode_mask();
    bsp_display_lvgl_lock();
    ui_page_detect_sync_controls();
    bsp_display_lvgl_unlock();
}

bool ui_get_continuous(void) { return s_continuous; }

void ui_request_single_shot(void)
{
    if (s_mode == UI_MODE_EDGE) s_shot_edge = true;
    else if (s_mode == UI_MODE_PC) s_shot_net = true;
}

bool ui_take_single_shot_edge(void)
{
    if (s_shot_edge) { s_shot_edge = false; return true; }
    return false;
}

bool ui_take_single_shot_net(void)
{
    if (s_shot_net) { s_shot_net = false; return true; }
    return false;
}

/* ---------------- 页面管理 ---------------- */

void ui_switch_page(int page_id)
{
    bsp_display_lvgl_lock();
    if (page_id == s_cur) {
        bsp_display_lvgl_unlock();
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (s_pages[i] == NULL) continue;
        if (i == page_id) lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_cur = page_id;

    /* 离开检测页时回退到纯预览,避免后台继续推流/推理 */
    if (page_id != UI_PAGE_DETECT && s_mode != UI_MODE_PREVIEW) {
        s_mode = UI_MODE_PREVIEW;
        s_continuous = false;
        apply_mode_mask();
    }
    if (page_id == UI_PAGE_CAPTURE) frame_hub_set_mask(FH_SUB_DISPLAY);

    switch (page_id) {
    case UI_PAGE_HOME:     ui_page_home_on_show(); break;
    case UI_PAGE_DETECT:   ui_page_detect_on_show(); break;
    case UI_PAGE_CAPTURE:  ui_page_capture_on_show(); break;
    case UI_PAGE_STATS:    ui_page_stats_on_show(); break;
    case UI_PAGE_SETTINGS: ui_page_settings_on_show(); break;
    default: break;
    }
    bsp_display_lvgl_unlock();
}

int ui_current_page(void) { return s_cur; }

void ui_init(void)
{
    s_preview_buf = heap_caps_malloc((size_t)UI_PREVIEW_W * UI_PREVIEW_H * 2,
                                     MALLOC_CAP_SPIRAM);
    if (s_preview_buf == NULL) {
        ESP_LOGE(TAG, "预览缓冲分配失败");
    } else {
        memset(&s_preview_dsc, 0, sizeof(s_preview_dsc));
        s_preview_dsc.header.w = UI_PREVIEW_W;
        s_preview_dsc.header.h = UI_PREVIEW_H;
        s_preview_dsc.header.stride = UI_PREVIEW_W * 2;
        s_preview_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        s_preview_dsc.data_size = (uint32_t)UI_PREVIEW_W * UI_PREVIEW_H * 2;
        s_preview_dsc.data = s_preview_buf;
    }

    ui_theme_init();

    lv_obj_t *scr = lv_screen_active();
    static const char *names[5] = { "home", "detect", "capture", "stats", "settings" };
    (void)names;
    for (int i = 0; i < 5; i++) {
        s_pages[i] = lv_obj_create(scr);
        lv_obj_set_size(s_pages[i], 240, 320);
        lv_obj_set_pos(s_pages[i], 0, 0);
        lv_obj_set_style_bg_color(s_pages[i], COL_BG, 0);
        lv_obj_set_style_border_width(s_pages[i], 0, 0);
        lv_obj_set_style_pad_all(s_pages[i], 0, 0);   /* 各页面自带 4px 边距布局 */
        lv_obj_set_style_radius(s_pages[i], 0, 0);
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pages[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    ui_page_home_create(s_pages[UI_PAGE_HOME]);
    ui_page_detect_create(s_pages[UI_PAGE_DETECT]);
    ui_page_capture_create(s_pages[UI_PAGE_CAPTURE]);
    ui_page_stats_create(s_pages[UI_PAGE_STATS]);
    ui_page_settings_create(s_pages[UI_PAGE_SETTINGS]);

    apply_mode_mask();
    ui_switch_page(UI_PAGE_HOME);
}

/* ---------------- 数据入口(跨任务) ---------------- */

static lv_image_dsc_t s_preview_dsc;              /* 描述符指向预览缓冲 */
static lv_obj_t *s_preview_imgs[2];
static int s_n_preview_imgs = 0;

/** 页面构建时注册预览 image 控件(最多 2 个:检测页/采集页)。 */
void ui_register_preview_img(lv_obj_t *img)
{
    if (s_n_preview_imgs < 2) s_preview_imgs[s_n_preview_imgs++] = img;
}

/** 预览图像描述符(data=共享预览缓冲)。 */
const lv_image_dsc_t *ui_preview_dsc(void)
{
    return &s_preview_dsc;
}

void ui_set_preview_frame(const frame_t *f)
{
    if (s_preview_buf == NULL || f == NULL) return;
    /* 先拷贝(相机任务上下文,fb 马上归还),再加锁通知 LVGL 重绘 */
    memcpy(s_preview_buf, f->buf, (size_t)UI_PREVIEW_W * UI_PREVIEW_H * 2);
    bsp_display_lvgl_lock();
    for (int i = 0; i < s_n_preview_imgs; i++) {
        lv_obj_invalidate(s_preview_imgs[i]);
    }
    bsp_display_lvgl_unlock();
}

void ui_show_edge_result(const edge_result_t *r)
{
    /* 单次(非连续)模式下计入统计 */
    if (!s_continuous && s_mode == UI_MODE_EDGE) {
        app_nvs_add_class_count(r->class_id, 1);
    }
    /* 检测页刷新(页面文件提供) */
    extern void ui_page_detect_show_edge(const edge_result_t *r);
    bsp_display_lvgl_lock();
    ui_page_detect_show_edge(r);
    bsp_display_lvgl_unlock();
}

void ui_show_pc_boxes(const uint8_t *payload, int len)
{
    /* 解析:u32 frame_id, u16 n, ×n{f32 x,y,w,h, u8 cls, u8 pad, f32 conf} */
    if (payload == NULL || len < 6) return;
    uint16_t n;
    memcpy(&n, payload + 4, 2);
    if (n > 16) n = 16;

    if (!s_continuous && s_mode == UI_MODE_PC) {
        /* 统计:最严重框(非 OK 类最高置信度;无框则 OK) */
        int stat_cls = 0;
        float best = 0;
        int off = 6;
        for (uint16_t i = 0; i < n && off + 20 <= len; i++, off += 20) {
            uint8_t cls = payload[off + 16];
            float conf;
            memcpy(&conf, payload + off + 18, 4);
            if (cls != 0 && conf > best) {
                best = conf;
                stat_cls = cls;
            }
        }
        app_nvs_add_class_count(stat_cls, 1);
    }

    extern void ui_page_detect_show_boxes(const uint8_t *payload, int len);
    bsp_display_lvgl_lock();
    ui_page_detect_show_boxes(payload, len);
    bsp_display_lvgl_unlock();
}

void ui_refresh_status(void)
{
    bsp_display_lvgl_lock();
    extern void ui_page_home_refresh(void);
    ui_page_home_refresh();
    ui_page_detect_refresh_status();
    bsp_display_lvgl_unlock();
}

/* ---------------- 按键分派 ---------------- */

void ui_on_key(int key, int event)
{
    if (event != 0) return;   /* 长按预留 */
    if (key == 2) {           /* K2:返回主页 */
        if (s_cur != UI_PAGE_HOME) ui_switch_page(UI_PAGE_HOME);
        return;
    }
    /* K1:当前页主操作 */
    switch (s_cur) {
    case UI_PAGE_DETECT:
        ui_request_single_shot();
        break;
    case UI_PAGE_CAPTURE:
        bsp_display_lvgl_lock();
        ui_page_capture_save();
        bsp_display_lvgl_unlock();
        break;
    default:
        break;
    }
}
