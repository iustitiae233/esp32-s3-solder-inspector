/* 检测页:预览 240x240 + bbox 叠加 + 结果条 + 模式控件 */
#include "lvgl.h"

#include <string.h>

#include "app_nvs.h"
#include "proto.h"
#include "ui.h"
#include "ui_theme.h"

#define BOX_POOL_N 8

static lv_obj_t *s_status;        /* 顶部状态条 */
static lv_obj_t *s_result;        /* 结果条:类别+置信度+耗时 */
static lv_obj_t *s_modes;         /* 模式按钮组 */
static lv_obj_t *s_btn_shot;      /* 单次按钮 */
static lv_obj_t *s_sw_cont;       /* 连续开关 */
static lv_obj_t *s_boxes[BOX_POOL_N];       /* bbox 叠加矩形池 */
static lv_obj_t *s_box_labels[BOX_POOL_N];

/* ---------- 模式/单次/连续 ---------- */

static void mode_event_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    const char *txt = lv_btnmatrix_get_button_text(bm, lv_btnmatrix_get_selected_button(bm));
    if (strcmp(txt, "预览") == 0) ui_set_mode(UI_MODE_PREVIEW);
    else if (strcmp(txt, "边缘") == 0) ui_set_mode(UI_MODE_EDGE);
    else if (strcmp(txt, "PC") == 0) ui_set_mode(UI_MODE_PC);
    ui_page_detect_sync_controls();
}

static void shot_event_cb(lv_event_t *e)
{
    (void)e;
    ui_request_single_shot();
}

static void cont_event_cb(lv_event_t *e)
{
    ui_set_continuous(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

/* ---------- 构建 ---------- */

void ui_page_detect_create(lv_obj_t *page)
{
    /* 预览(0,0..239,239) */
    lv_obj_t *img = lv_image_create(page);
    lv_image_set_src(img, ui_preview_dsc());
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_size(img, UI_PREVIEW_W, UI_PREVIEW_H);
    ui_register_preview_img(img);

    /* bbox 池:边框矩形+角标,叠在预览上 */
    for (int i = 0; i < BOX_POOL_N; i++) {
        s_boxes[i] = lv_obj_create(page);
        lv_obj_set_size(s_boxes[i], 10, 10);
        lv_obj_set_style_border_width(s_boxes[i], 2, 0);
        lv_obj_set_style_border_color(s_boxes[i], COL_WARN, 0);
        lv_obj_set_style_radius(s_boxes[i], 0, 0);
        lv_obj_set_style_bg_opa(s_boxes[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(s_boxes[i], 0, 0);
        lv_obj_add_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_boxes[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        s_box_labels[i] = lv_label_create(s_boxes[i]);
        lv_obj_set_style_text_font(s_box_labels[i], &lv_font_simsun_16_cjk, 0);
        lv_obj_set_style_text_color(s_box_labels[i], COL_WARN, 0);
        lv_label_set_text(s_box_labels[i], "");
        lv_obj_align(s_box_labels[i], LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    }

    /* 状态条 y=240..263 */
    s_status = lv_label_create(page);
    lv_obj_set_style_text_font(s_status, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(s_status, COL_TEXT_DIM, 0);
    lv_label_set_text(s_status, "待机");
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 0, 242);

    /* 结果条 y=264..283 */
    s_result = lv_label_create(page);
    lv_obj_set_style_text_font(s_result, &lv_font_simsun_16_cjk, 0);
    lv_label_set_text(s_result, "--");
    lv_obj_align(s_result, LV_ALIGN_TOP_LEFT, 0, 265);

    /* 控件行 y=286..320:模式组 | 单次 | 连续 */
    s_modes = lv_btnmatrix_create(page);
    static const char *map[] = { "预览", "边缘", "PC", "" };
    lv_btnmatrix_set_map(s_modes, map);
    lv_btnmatrix_set_one_checked(s_modes, true);
    lv_obj_set_size(s_modes, 118, 34);
    lv_obj_align(s_modes, LV_ALIGN_TOP_LEFT, 0, 286);
    lv_obj_add_event_cb(s_modes, mode_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_btn_shot = ui_create_btn(page, "单次", COL_PRIMARY);
    lv_obj_set_size(s_btn_shot, 50, 34);
    lv_obj_align(s_btn_shot, LV_ALIGN_TOP_LEFT, 120, 286);
    lv_obj_add_event_cb(s_btn_shot, shot_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_c = lv_label_create(page);
    lv_label_set_text(lbl_c, "连续");
    lv_obj_set_style_text_font(lbl_c, &lv_font_simsun_16_cjk, 0);
    lv_obj_set_style_text_color(lbl_c, COL_TEXT_DIM, 0);
    lv_obj_align(lbl_c, LV_ALIGN_TOP_LEFT, 172, 292);

    s_sw_cont = lv_switch_create(page);
    lv_obj_set_size(s_sw_cont, 44, 28);
    lv_obj_align(s_sw_cont, LV_ALIGN_TOP_LEFT, 198, 289);
    lv_obj_add_event_cb(s_sw_cont, cont_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------- 显示逻辑(持 LVGL 锁调用) ---------- */

void ui_page_detect_sync_controls(void)
{
    lv_btnmatrix_set_button_checked(s_modes,
        ui_get_mode() == UI_MODE_PREVIEW ? 0 : ui_get_mode() == UI_MODE_EDGE ? 1 : 2, true);
    if (ui_get_continuous()) lv_obj_add_state(s_sw_cont, LV_STATE_CHECKED);
    else lv_obj_remove_state(s_sw_cont, LV_STATE_CHECKED);
}

void ui_page_detect_refresh_status(void)
{
    switch (ui_get_mode()) {
    case UI_MODE_PREVIEW: lv_label_set_text(s_status, "预览模式"); break;
    case UI_MODE_EDGE:    lv_label_set_text(s_status, edge_ai_ready() ? "边缘推理" : "边缘推理(无模型!)"); break;
    case UI_MODE_PC:      lv_label_set_text(s_status, "PC 检测"); break;
    }
}

void ui_page_detect_on_show(void)
{
    ui_page_detect_sync_controls();
    ui_page_detect_refresh_status();
}

void ui_page_detect_show_edge(const edge_result_t *r)
{
    if (r == NULL) return;
    bool ok = (r->class_id == 0);
    lv_obj_set_style_text_color(s_result, ok ? COL_OK : COL_WARN, 0);
    lv_label_set_text_fmt(s_result, "%s %d%%  %dms",
                          edge_ai_label(r->class_id),
                          (int)(r->confidence * 100), r->inference_ms);
    for (int i = 0; i < BOX_POOL_N; i++) lv_obj_add_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
}

void ui_page_detect_show_boxes(const uint8_t *payload, int len)
{
    if (payload == NULL || len < 6) return;
    uint16_t n;
    memcpy(&n, payload + 4, 2);
    int shown = 0, ng = 0;
    int off = 6;
    for (uint16_t i = 0; i < n && off + PROTO_DETECT_BOX_LEN <= len && shown < BOX_POOL_N;
         i++, off += PROTO_DETECT_BOX_LEN) {
        float x, y, w, h, conf;
        memcpy(&x, payload + off + 0, 4);
        memcpy(&y, payload + off + 4, 4);
        memcpy(&w, payload + off + 8, 4);
        memcpy(&h, payload + off + 12, 4);
        uint8_t cls = payload[off + 16];
        memcpy(&conf, payload + off + 18, 4);
        if (cls != 0) ng++;

        lv_obj_t *box = s_boxes[shown];
        lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(box, (int32_t)x, (int32_t)y);
        lv_obj_set_size(box, (int32_t)w < 8 ? 8 : (int32_t)w,
                              (int32_t)h < 8 ? 8 : (int32_t)h);
        lv_color_t c = (cls == 0) ? COL_OK : COL_WARN;
        lv_obj_set_style_border_color(box, c, 0);
        lv_obj_set_style_text_color(s_box_labels[shown], c, 0);
        lv_label_set_text_fmt(s_box_labels[shown], "%s%d%%",
                              edge_ai_label(cls), (int)(conf * 100));
        shown++;
    }
    for (int i = shown; i < BOX_POOL_N; i++) {
        lv_obj_add_flag(s_boxes[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 结果条:PC 模式检出汇总 */
    lv_obj_set_style_text_color(s_result, ng ? COL_WARN : COL_OK, 0);
    if (n == 0) lv_label_set_text(s_result, "PC:未检出目标");
    else if (ng) lv_label_set_text_fmt(s_result, "PC:NG %d / 共 %d", ng, n);
    else lv_label_set_text_fmt(s_result, "PC:全部 OK(%d)", n);
}
