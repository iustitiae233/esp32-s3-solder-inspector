#include "ui_theme.h"

LV_FONT_DECLARE(lv_font_simsun_16_cjk);

void ui_theme_init(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_text_color(scr, COL_TEXT, 0);
    lv_obj_set_style_text_font(scr, &lv_font_simsun_16_cjk, 0);
}

void ui_style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, COL_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
}

lv_obj_t *ui_create_btn(lv_obj_t *parent, const char *text, lv_color_t accent)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_style_bg_color(btn, accent, 0);
    lv_obj_set_style_bg_color(btn, lv_color_darken(accent, LV_OPA_30), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}
