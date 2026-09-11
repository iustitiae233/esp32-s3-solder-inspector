/* 采集页:预览 + 类别选择 + 保存到 /storage/captures */
#include "lvgl.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui.h"
#include "ui_theme.h"

static lv_obj_t *s_chips[6];
static lv_obj_t *s_btn_save;
static int s_sel = 0;

static void chip_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    s_sel = idx;
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_bg_color(s_chips[i], i == s_sel ? COL_PRIMARY : COL_CARD_HI, 0);
    }
}

/* RGB565 小端 → 24bit BMP(行序自下而上, BGR) */
static int save_bmp24(const char *path, const uint8_t *rgb565)
{
    const int W = UI_PREVIEW_W, H = UI_PREVIEW_H;
    const int row_bytes = W * 3;                     /* 720,已 4 字节对齐 */
    const int data_size = row_bytes * H;
    uint8_t hdr[54] = { 0 };
    uint32_t u;

    hdr[0] = 'B'; hdr[1] = 'M';
    u = 54 + data_size; memcpy(hdr + 2, &u, 4);
    u = 54;            memcpy(hdr + 10, &u, 4);
    u = 40;            memcpy(hdr + 14, &u, 4);
    u = W;             memcpy(hdr + 18, &u, 4);
    u = H;             memcpy(hdr + 22, &u, 4);
    hdr[26] = 1; hdr[28] = 24;

    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    if (fwrite(hdr, 1, 54, fp) != 54) { fclose(fp); return -1; }

    uint8_t row[row_bytes];
    for (int y = H - 1; y >= 0; y--) {
        const uint16_t *src = (const uint16_t *)rgb565 + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            uint16_t px = src[x];
            row[x * 3 + 0] = (uint8_t)((px & 0x1F) * 255 / 31);          /* B */
            row[x * 3 + 1] = (uint8_t)(((px >> 5) & 0x3F) * 255 / 63);   /* G */
            row[x * 3 + 2] = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);  /* R */
        }
        if (fwrite(row, 1, row_bytes, fp) != (size_t)row_bytes) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

void ui_page_capture_save(void)
{
    if (edge_ai_class_count() <= 0 || s_sel >= edge_ai_class_count()) {
        return;
    }
    const char *label = edge_ai_label(s_sel);
    char dir[96], path[128];
    snprintf(dir, sizeof(dir), "/storage/captures/%s", label);
    mkdir("/storage/captures", 0775);
    mkdir(dir, 0775);
    snprintf(path, sizeof(path), "%s/%u.bmp", dir,
             (unsigned)(xTaskGetTickCount() & 0x7FFFFFFF));

    const lv_image_dsc_t *dsc = ui_preview_dsc();
    lv_obj_t *lbl = lv_obj_get_child(s_btn_save, 0);
    if (dsc->data == NULL || save_bmp24(path, dsc->data) != 0) {
        lv_label_set_text(lbl, "保存失败");
    } else {
        uint64_t total = 0, freeb = 0;
        if (esp_vfs_fat_info("/storage", &total, &freeb) == ESP_OK) {
            lv_label_set_text_fmt(lbl, "%s 已存(余%uKB)", label, (unsigned)(freeb / 1024));
        } else {
            lv_label_set_text_fmt(lbl, "%s 已存", label);
        }
    }
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    ui_page_capture_save();
}

void ui_page_capture_create(lv_obj_t *page)
{
    lv_obj_t *img = lv_image_create(page);
    lv_image_set_src(img, ui_preview_dsc());
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_size(img, UI_PREVIEW_W, UI_PREVIEW_H);
    ui_register_preview_img(img);

    /* 类别 chips:3 列 x2 行(240..287) */
    for (int i = 0; i < 6; i++) {
        s_chips[i] = lv_button_create(page);
        lv_obj_set_size(s_chips[i], 77, 20);
        lv_obj_set_pos(s_chips[i], 1 + (i % 3) * 79, 242 + (i / 3) * 22);
        lv_obj_set_style_bg_color(s_chips[i], i == 0 ? COL_PRIMARY : COL_CARD_HI, 0);
        lv_obj_set_style_radius(s_chips[i], 4, 0);
        lv_obj_set_style_pad_all(s_chips[i], 0, 0);
        lv_obj_add_event_cb(s_chips[i], chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(s_chips[i]);
        lv_label_set_text(l, edge_ai_label(i));
        lv_obj_center(l);
    }

    /* 保存按钮(K1 同效) */
    s_btn_save = ui_create_btn(page, "保存图像(K1)", COL_OK);
    lv_obj_set_size(s_btn_save, 228, 26);
    lv_obj_set_pos(s_btn_save, 1, 288);
    lv_obj_add_event_cb(s_btn_save, save_cb, LV_EVENT_CLICKED, NULL);
}

void ui_page_capture_on_show(void)
{
    /* 刷新 chips 文本(模型加载前后类别名可能变化) */
    for (int i = 0; i < 6; i++) {
        lv_obj_t *l = lv_obj_get_child(s_chips[i], 0);
        if (l) lv_label_set_text(l, edge_ai_label(i));
    }
    lv_obj_t *lbl = lv_obj_get_child(s_btn_save, 0);
    if (lbl) lv_label_set_text(lbl, "保存图像(K1)");
}
