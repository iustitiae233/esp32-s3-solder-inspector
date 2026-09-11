#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

/* 深色工业主题配色 */
#define COL_BG       lv_color_hex(0x1A1D23)
#define COL_CARD     lv_color_hex(0x242933)
#define COL_CARD_HI  lv_color_hex(0x2E3542)
#define COL_PRIMARY  lv_color_hex(0x00B4D8)
#define COL_WARN     lv_color_hex(0xFF6B6B)
#define COL_OK       lv_color_hex(0x2ECC71)
#define COL_TEXT     lv_color_hex(0xE8EAED)
#define COL_TEXT_DIM lv_color_hex(0x9AA3AF)

/** 全局主题:默认字体(CJK)、背景色、默认样式。 */
void ui_theme_init(void);

/** 应用卡片样式(圆角/底色/内边距)。 */
void ui_style_card(lv_obj_t *obj);

/** 创建带强调色的按钮(全宽或自适应由调用方布局)。 */
lv_obj_t *ui_create_btn(lv_obj_t *parent, const char *text, lv_color_t accent);

#endif /* UI_THEME_H */
