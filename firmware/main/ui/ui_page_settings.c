/* 设置页:WiFi / PC 地址 / 阈值 / 背光 / 校准 / 恢复默认(可滚动 + 底部键盘) */
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_nvs.h"
#include "bsp_backlight.h"
#include "pc_link.h"
#include "ui.h"
#include "ui_theme.h"

typedef enum {
    EDIT_NONE = 0,
    EDIT_SSID,
    EDIT_PASS,
    EDIT_PCIP,
    EDIT_PCPORT,
} edit_target_t;

static lv_obj_t *s_kb;
static lv_obj_t *s_ta;
static lv_obj_t *s_edit_panel;
static lv_obj_t *s_lbl_edit;      /* 编辑面板标题 */
static lv_obj_t *s_lbl_ssid;
static lv_obj_t *s_lbl_pc;
static lv_obj_t *s_slider_thr;
static lv_obj_t *s_slider_bl;
static edit_target_t s_editing = EDIT_NONE;

/* ---------- 键盘编辑 ---------- */

static void kb_ready_cb(lv_event_t *e)
{
    const char *txt = lv_textarea_get_text(s_ta);
    char ip[24] = { 0 };
    uint16_t port = 3333;
    switch (s_editing) {
    case EDIT_SSID: {
        char pass[64];
        app_nvs_get_wifi(ip, sizeof(ip), pass, sizeof(pass));   /* 复用缓冲读旧密码 */
        app_nvs_set_wifi(txt, pass);
        break;
    }
    case EDIT_PASS: {
        char ssid[32];
        app_nvs_get_wifi(ssid, sizeof(ssid), ip, sizeof(ip));
        app_nvs_set_wifi(ssid, txt);
        break;
    }
    case EDIT_PCIP: {
        uint16_t p;
        app_nvs_get_pc_addr(ip, sizeof(ip), &p);
        app_nvs_set_pc_addr(txt, p);
        pc_link_retarget(txt, p);   /* 断开并按新地址重连 */
        break;
    }
    case EDIT_PCPORT: {
        uint16_t p = (uint16_t)atoi(txt);
        if (p == 0) p = 3333;
        app_nvs_get_pc_addr(ip, sizeof(ip), &port);
        app_nvs_set_pc_addr(ip, p);
        pc_link_retarget(ip, p);
        break;
    }
    default:
        break;
    }
    lv_obj_add_flag(s_edit_panel, LV_OBJ_FLAG_HIDDEN);
    s_editing = EDIT_NONE;
    (void)e;
}

static void kb_cancel_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_edit_panel, LV_OBJ_FLAG_HIDDEN);
    s_editing = EDIT_NONE;
}

static void start_edit(edit_target_t target, const char *title, const char *cur)
{
    s_editing = target;
    lv_obj_remove_flag(s_edit_panel, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_edit, title);
    lv_textarea_set_text(s_ta, cur);
    lv_keyboard_set_textarea(s_kb, s_ta);
}

/* ---------- 各行点击 ---------- */

static void ssid_cb(lv_event_t *e)
{
    (void)e;
    char ssid[32], pass[64];
    if (!app_nvs_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) ssid[0] = 0;
    start_edit(EDIT_SSID, "WiFi 名称", ssid);
}

static void pass_cb(lv_event_t *e)
{
    (void)e;
    char ssid[32], pass[64];
    if (!app_nvs_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) pass[0] = 0;
    start_edit(EDIT_PASS, "WiFi 密码", pass);
}

static void pcip_cb(lv_event_t *e)
{
    (void)e;
    char ip[24];
    uint16_t port;
    app_nvs_get_pc_addr(ip, sizeof(ip), &port);
    start_edit(EDIT_PCIP, "PC IP 地址", ip);
}

static void pcport_cb(lv_event_t *e)
{
    (void)e;
    char ip[24], buf[8];
    uint16_t port;
    app_nvs_get_pc_addr(ip, sizeof(ip), &port);
    snprintf(buf, sizeof(buf), "%u", (unsigned)port);
    start_edit(EDIT_PCPORT, "PC 端口", buf);
}

static void thr_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    app_nvs_set_threshold((int)lv_slider_get_value(slider));
}

static void bl_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    app_nvs_set_backlight(v);
    bsp_backlight_set(v);
}

static void calib_cb(lv_event_t *e)
{
    (void)e;
    extern void ui_calib_enter(void);
    ui_calib_enter();
}

static void reset_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *txt = lv_msgbox_get_active_button_text(mbox);
    if (txt && strcmp(txt, "恢复") == 0) {
        app_nvs_set_wifi("", "");
        app_nvs_set_pc_addr("192.168.1.100", 3333);
        app_nvs_set_threshold(60);
        app_nvs_set_backlight(80);
        bsp_backlight_set(80);
    }
    lv_obj_delete(mbox);
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    static const char *btns[] = { "恢复", "取消", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, "恢复默认", "清除 WiFi/PC/阈值/背光设置?(统计保留)", btns, false);
    lv_obj_add_event_cb(mbox, reset_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

/* ---------- 构建 ---------- */

static lv_obj_t *make_row(lv_obj_t *page, const char *name, int y, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_button_create(page);
    lv_obj_set_size(row, 228, 34);
    lv_obj_set_pos(row, 4, y);
    lv_obj_set_style_bg_color(row, COL_CARD, 0);
    lv_obj_set_style_bg_color(row, COL_CARD_HI, LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_label_set_text(l, name);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, 0);
    return row;
}

void ui_page_settings_create(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(title, COL_PRIMARY, 0);
    lv_label_set_text(title, "设置");
    lv_obj_set_pos(title, 4, 2);

    /* WiFi / PC(点击弹键盘) */
    lv_obj_t *r1 = make_row(page, "WiFi 名称", 26, ssid_cb);
    s_lbl_ssid = lv_label_create(r1);
    lv_obj_set_style_text_font(s_lbl_ssid, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_lbl_ssid, COL_TEXT_DIM, 0);
    lv_obj_align(s_lbl_ssid, LV_ALIGN_RIGHT_MID, -4, 0);

    make_row(page, "WiFi 密码", 64, pass_cb);

    lv_obj_t *r3 = make_row(page, "PC 地址", 102, pcip_cb);
    s_lbl_pc = lv_label_create(r3);
    lv_obj_set_style_text_font(s_lbl_pc, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_lbl_pc, COL_TEXT_DIM, 0);
    lv_obj_align(s_lbl_pc, LV_ALIGN_RIGHT_MID, -4, 0);

    make_row(page, "PC 端口", 140, pcport_cb);

    /* 阈值滑条 */
    lv_obj_t *lt = lv_label_create(page);
    lv_obj_set_style_text_font(lt, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(lt, "置信度阈值");
    lv_obj_set_pos(lt, 4, 182);
    lv_obj_t *thr = lv_slider_create(page);
    lv_obj_set_size(thr, 228, 16);
    lv_obj_set_pos(thr, 4, 202);
    lv_slider_set_range(thr, 1, 99);
    lv_obj_add_event_cb(thr, thr_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_slider_thr = thr;

    /* 背光滑条 */
    lv_obj_t *lb = lv_label_create(page);
    lv_obj_set_style_text_font(lb, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(lb, "屏幕背光");
    lv_obj_set_pos(lb, 4, 226);
    lv_obj_t *bl = lv_slider_create(page);
    lv_obj_set_size(bl, 228, 16);
    lv_obj_set_pos(bl, 4, 246);
    lv_slider_set_range(bl, 0, 100);
    lv_obj_add_event_cb(bl, bl_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_slider_bl = bl;

    /* 校准 / 恢复默认 */
    lv_obj_t *bcal = ui_create_btn(page, "触摸校准", COL_PRIMARY);
    lv_obj_set_size(bcal, 110, 34);
    lv_obj_set_pos(bcal, 4, 270);
    lv_obj_add_event_cb(bcal, calib_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *brst = ui_create_btn(page, "恢复默认", COL_WARN);
    lv_obj_set_size(brst, 110, 34);
    lv_obj_set_pos(brst, 118, 270);
    lv_obj_add_event_cb(brst, reset_cb, LV_EVENT_CLICKED, NULL);

    /* 编辑面板(标题 + 文本框 + 键盘,默认隐藏,覆盖全屏) */
    s_edit_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_edit_panel, 240, 320);
    lv_obj_set_pos(s_edit_panel, 0, 0);
    lv_obj_set_style_bg_color(s_edit_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(s_edit_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_edit_panel, 0, 0);
    lv_obj_set_style_radius(s_edit_panel, 0, 0);
    lv_obj_add_flag(s_edit_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *etitle = lv_label_create(s_edit_panel);
    lv_obj_set_style_text_font(etitle, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(etitle, COL_TEXT, 0);
    lv_label_set_text(etitle, "输入");
    lv_obj_set_pos(etitle, 6, 4);
    s_lbl_edit = etitle;

    s_ta = lv_textarea_create(s_edit_panel);
    lv_obj_set_size(s_ta, 228, 40);
    lv_obj_set_pos(s_ta, 4, 26);
    lv_textarea_set_one_line(s_ta, true);

    s_kb = lv_keyboard_create(s_edit_panel);
    lv_obj_set_size(s_kb, 240, 240);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(s_kb, kb_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, kb_cancel_cb, LV_EVENT_CANCEL, NULL);
}

void ui_page_settings_on_show(void)
{
    char ssid[32], pass[64], ip[24];
    uint16_t port;
    if (app_nvs_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) && ssid[0]) {
        lv_label_set_text(s_lbl_ssid, ssid);
    } else {
        lv_label_set_text(s_lbl_ssid, "未配置");
    }
    app_nvs_get_pc_addr(ip, sizeof(ip), &port);
    lv_label_set_text_fmt(s_lbl_pc, "%s:%u", ip, (unsigned)port);
    lv_slider_set_value(s_slider_thr, app_nvs_get_threshold(), LV_ANIM_OFF);
    lv_slider_set_value(s_slider_bl, app_nvs_get_backlight(), LV_ANIM_OFF);
}
