/* 主页:标题 + 状态卡 + 四入口 */
#include "lvgl.h"

#include <stdio.h>

#include "app_nvs.h"
#include "bsp_display.h"
#include "edge_ai.h"
#include "pc_link.h"
#include "ui.h"
#include "ui_theme.h"
#include "wifi_sta.h"

static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_pc;
static lv_obj_t *s_lbl_model;

static void goto_detect(lv_event_t *e) { ui_switch_page(UI_PAGE_DETECT); }
static void goto_capture(lv_event_t *e) { ui_switch_page(UI_PAGE_CAPTURE); }
static void goto_stats(lv_event_t *e) { ui_switch_page(UI_PAGE_STATS); }
static void goto_settings(lv_event_t *e) { ui_switch_page(UI_PAGE_SETTINGS); }

void ui_page_home_create(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(title, "PCB 焊点检测仪");
    lv_obj_set_style_text_color(title, COL_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* 状态卡 */
    lv_obj_t *card = lv_obj_create(page);
    ui_style_card(card);
    lv_obj_set_size(card, 228, 86);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_text_color(lv_scr_act(), COL_TEXT, 0);
    s_lbl_wifi = lv_label_create(card);
    lv_label_set_text(s_lbl_wifi, "WiFi: --");
    lv_obj_align(s_lbl_wifi, LV_ALIGN_TOP_LEFT, 0, 2);

    s_lbl_pc = lv_label_create(card);
    lv_label_set_text(s_lbl_pc, "PC : --");
    lv_obj_align(s_lbl_pc, LV_ALIGN_TOP_LEFT, 0, 26);

    s_lbl_model = lv_label_create(card);
    lv_label_set_text(s_lbl_model, "模型: --");
    lv_obj_align(s_lbl_model, LV_ALIGN_TOP_LEFT, 0, 50);

    /* 2x2 入口 */
    static const char *texts[4] = { "检 测", "采 集", "统 计", "设 置" };
    static const lv_color_t accents[4] = { COL_PRIMARY, COL_OK, COL_WARN, COL_CARD_HI };
    static lv_obj_t *btns[4];
    for (int i = 0; i < 4; i++) {
        btns[i] = ui_create_btn(page, texts[i], accents[i]);
        lv_obj_set_size(btns[i], 109, 62);
        int col = i % 2, row = i / 2;
        lv_obj_align(btns[i], LV_ALIGN_TOP_LEFT, 4 + col * 113, 122 + row * 68);
    }
    lv_obj_add_event_cb(btns[0], goto_detect, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btns[1], goto_capture, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btns[2], goto_stats, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btns[3], goto_settings, LV_EVENT_CLICKED, NULL);
}

void ui_page_home_refresh(void)
{
    char ip[20];
    if (wifi_sta_is_up()) {
        wifi_sta_get_ip(ip, sizeof(ip));
        lv_label_set_text_fmt(s_lbl_wifi, "WiFi: 已连接 %s", ip);
    } else {
        lv_label_set_text(s_lbl_wifi, "WiFi: 未连接");
    }
    lv_label_set_text_fmt(s_lbl_pc, "PC : %s", pc_link_is_up() ? "已连接" : "未连接");
    if (edge_ai_ready()) {
        lv_label_set_text_fmt(s_lbl_model, "模型: 已加载(%d 类)", edge_ai_class_count());
    } else {
        lv_label_set_text(s_lbl_model, "模型: 未加载");
    }
}

void ui_page_home_on_show(void)
{
    ui_page_home_refresh();
}
