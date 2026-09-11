/* 统计页:各类计数 + 条形图 + NG 率 + 清零 */
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#include "app_nvs.h"
#include "edge_ai.h"
#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_rows[6];       /* 每类:label + bar + count */
static lv_obj_t *s_lbl_total;
static lv_obj_t *s_lbl_ng;

static void refresh(void)
{
    uint32_t total = 0, ng = 0;
    uint32_t maxv = 1;
    uint32_t counts[6] = { 0 };
    int n = edge_ai_class_count();
    if (n > 6) n = 6;

    for (int i = 0; i < n; i++) {
        counts[i] = app_nvs_get_class_count(i);
        total += counts[i];
        if (i != 0) ng += counts[i];
        if (counts[i] > maxv) maxv = counts[i];
    }
    for (int i = 0; i < 6; i++) {
        if (s_rows[i] == NULL) continue;
        lv_obj_t *lbl = lv_obj_get_child(s_rows[i], 0);
        lv_obj_t *bar = lv_obj_get_child(s_rows[i], 1);
        lv_obj_t *cnt = lv_obj_get_child(s_rows[i], 2);
        if (i < n) {
            lv_label_set_text_fmt(lbl, "%s", edge_ai_label(i));
            lv_bar_set_range(bar, 0, (int32_t)maxv);
            lv_bar_set_value(bar, (int32_t)counts[i], LV_ANIM_OFF);
            lv_label_set_text_fmt(cnt, "%u", (unsigned)counts[i]);
        } else {
            lv_label_set_text(lbl, "-");
            lv_bar_set_value(bar, 0, LV_ANIM_OFF);
            lv_label_set_text(cnt, "");
        }
    }
    lv_label_set_text_fmt(s_lbl_total, "总计 %u", (unsigned)total);
    lv_label_set_text_fmt(s_lbl_ng, "NG 率 %.1f%%", total ? 100.0 * ng / total : 0.0);
}

static void clear_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *txt = lv_msgbox_get_active_button_text(mbox);
    if (txt && strcmp(txt, "清零") == 0) {
        app_nvs_clear_class_counts();
        refresh();
    }
    lv_obj_delete(mbox);
}

static void clear_btn_cb(lv_event_t *e)
{
    (void)e;
    static const char *btns[] = { "清零", "取消", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, "确认", "清空全部统计数据?", btns, false);
    lv_obj_add_event_cb(mbox, clear_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

void ui_page_stats_create(lv_obj_t *page)
{
    lv_obj_t *title = lv_label_create(page);
    lv_obj_set_style_text_font(title, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(title, COL_PRIMARY, 0);
    lv_label_set_text(title, "检测统计");
    lv_obj_set_pos(title, 4, 2);

    s_lbl_total = lv_label_create(page);
    lv_obj_set_style_text_font(s_lbl_total, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(s_lbl_total, "总计 0");
    lv_obj_set_pos(s_lbl_total, 96, 2);

    s_lbl_ng = lv_label_create(page);
    lv_obj_set_style_text_font(s_lbl_ng, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_lbl_ng, COL_WARN, 0);
    lv_label_set_text(s_lbl_ng, "NG 率 0.0%");
    lv_obj_set_pos(s_lbl_ng, 168, 2);

    for (int i = 0; i < 6; i++) {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_size(row, 228, 32);
        lv_obj_set_pos(row, 4, 26 + i * 37);
        ui_style_card(row);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(row, 2, 0);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_font(lbl, &lv_font_simsun_16_cjk, 0);
        lv_label_set_text(lbl, "-");
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 2, 0);

        lv_obj_t *bar = lv_bar_create(row);
        lv_obj_set_size(bar, 108, 12);
        lv_obj_align(bar, LV_ALIGN_LEFT_MID, 56, 0);
        lv_bar_set_range(bar, 0, 100);
        lv_obj_set_style_bg_color(bar, COL_CARD_HI, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, i == 0 ? COL_OK : COL_WARN, LV_PART_INDICATOR);

        lv_obj_t *cnt = lv_label_create(row);
        lv_obj_align(cnt, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_label_set_text(cnt, "");

        s_rows[i] = row;
    }

    lv_obj_t *btn = ui_create_btn(page, "清零", COL_WARN);
    lv_obj_set_size(btn, 228, 30);
    lv_obj_set_pos(btn, 4, 26 + 6 * 37 + 4);
    lv_obj_add_event_cb(btn, clear_btn_cb, LV_EVENT_CLICKED, NULL);
}

void ui_page_stats_on_show(void)
{
    refresh();
}
