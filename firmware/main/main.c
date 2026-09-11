/* 焊点缺陷检测仪 —— 应用入口:初始化序列 + 相机采集任务 + 订阅接线 */
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "wear_levelling.h"

#include "app_nvs.h"
#include "bsp_backlight.h"
#include "bsp_camera.h"
#include "bsp_display.h"
#include "bsp_key.h"
#include "bsp_touch.h"
#include "edge_ai.h"
#include "frame_hub.h"
#include "pc_link.h"
#include "proto.h"
#include "ui.h"
#include "wifi_sta.h"

static const char *TAG = "main";

static wl_handle_t s_wl = WL_INVALID_HANDLE;

/* ---------------- 帧订阅回调(相机任务上下文) ---------------- */

static void on_display_frame(const frame_t *f)
{
    ui_set_preview_frame(f);
}

static void on_edge_frame(const frame_t *f)
{
    edge_result_t r;
    if (edge_ai_classify(f, &r) == 0) {
        ui_show_edge_result(&r);
    }
}

static void on_net_frame(const frame_t *f)
{
    pc_link_send_image(f);
}

/* ---------------- PC 下行:检测结果 / 命令 ---------------- */

static void on_pc_detect(const uint8_t *payload, int len)
{
    ui_show_pc_boxes(payload, len);
}

static void on_pc_command(uint8_t cmd, uint8_t arg)
{
    (void)arg;
    switch (cmd) {
    case PROTO_CMD_STREAM_START:
        ui_switch_page(UI_PAGE_DETECT);
        ui_set_mode(UI_MODE_PC);
        ui_set_continuous(true);
        break;
    case PROTO_CMD_STREAM_STOP:
        ui_set_continuous(false);
        break;
    case PROTO_CMD_SINGLE_SHOT:
        ui_switch_page(UI_PAGE_DETECT);
        ui_set_mode(UI_MODE_PC);
        ui_request_single_shot();
        break;
    default:
        break;
    }
}

/* ---------------- 任务 ---------------- */

/* 相机采集:取帧 → 组装 frame_t → (掩码|单次位)分发 → 归还 */
static void camera_task(void *arg)
{
    (void)arg;
    uint32_t frame_id = 0;
    ESP_LOGI(TAG, "采集任务启动");
    for (;;) {
        camera_fb_t *fb = bsp_camera_fb_get(pdMS_TO_TICKS(200));
        if (fb == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        frame_t f = {
            .buf = fb->buf,
            .frame_id = frame_id++,
            .timestamp = xTaskGetTickCount(),
        };
        uint32_t mask = frame_hub_get_mask();
        if (ui_take_single_shot_edge()) mask |= FH_SUB_EDGE_AI;
        if (ui_take_single_shot_net()) mask |= FH_SUB_NET;
        frame_hub_dispatch_masked(mask, &f);
        bsp_camera_fb_return(fb);
        /* 10fps 预览足够;连续检测时下游(推理/网络)本身阻塞节流 */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* 周期刷新状态显示(主页状态卡/检测页状态条) */
static void status_task(void *arg)
{
    (void)arg;
    for (;;) {
        ui_refresh_status();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* 网络:WiFi 阻塞连接(带凭据时)→ 首次联网后启动 PC 链路(仅一次) */
static void net_task(void *arg)
{
    (void)arg;
    bool link_started = false;
    char ssid[32], pass[64];
    for (;;) {
        if (!wifi_sta_is_up()) {
            app_nvs_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
            if (ssid[0]) {
                ESP_LOGI(TAG, "连接 WiFi: %s", ssid);
                wifi_sta_start(ssid, pass);   /* 阻塞 ≤15s,失败后台继续重连 */
            }
        }
        if (wifi_sta_is_up() && !link_started) {
            char ip[24];
            uint16_t port;
            app_nvs_get_pc_addr(ip, sizeof(ip), &port);
            ESP_LOGI(TAG, "启动 PC 链路 %s:%u", ip, (unsigned)port);
            pc_link_start(ip, port);         /* 任务内部 3s 自动重连 */
            link_started = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ---------------- 初始化失败提示(仍可用触摸进设置) ---------------- */

static void show_fatal(const char *msg)
{
    bsp_display_lvgl_lock();
    lv_obj_t *lbl = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF6B6B), 0);
    lv_obj_set_style_bg_color(lbl, lv_color_hex(0x1A1D23), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lbl, 8, 0);
    lv_label_set_text(lbl, msg);
    lv_obj_center(lbl);
    bsp_display_lvgl_unlock();
}

/* ---------------- app_main ---------------- */

void app_main(void)
{
    /* NVS(WiFi 校准/配置存储) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    app_nvs_open_all();

    /* 网络协议栈(wifi_sta 依赖) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* FATFS:/storage(模型/标签/采集图像) */
    esp_vfs_fat_mount_config_t mc = {
        .max_files = 8,
        .format_if_mount_failed = true,
        .allocation_unit_size = 4096,
    };
    err = esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &mc, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "/storage 挂载失败: %s(模型与采集不可用)", esp_err_to_name(err));
    }

    /* 背光 → 显示 → 触摸(共享 SPI2,顺序不可变) */
    bsp_backlight_init();
    bsp_backlight_set(app_nvs_get_backlight());
    if (bsp_display_init() != 0) {
        ESP_LOGE(TAG, "显示初始化失败,系统挂起");
        vTaskDelay(portMAX_DELAY);
    }
    bsp_display_lvgl_lock();
    int trc = bsp_touch_init();
    ui_init();
    bsp_display_lvgl_unlock();
    if (trc != 0) ESP_LOGW(TAG, "触摸不可用(仅按键操作)");

    /* 按键 → UI */
    bsp_key_init();
    bsp_key_set_cb(ui_on_key);

    /* 边缘 AI(/storage 模型缺失时界面显示"未加载") */
    if (edge_ai_load() != 0) {
        ESP_LOGW(TAG, "边缘模型未加载,边缘模式禁用");
    }

    /* 帧分发接线:顺序即回调顺序 DISPLAY→EDGE_AI→NET */
    frame_hub_on_frame(FH_SUB_DISPLAY, on_display_frame);
    frame_hub_on_frame(FH_SUB_EDGE_AI, on_edge_frame);
    frame_hub_on_frame(FH_SUB_NET, on_net_frame);

    /* PC 下行 */
    pc_link_set_on_detect(on_pc_detect);
    pc_link_set_on_command(on_pc_command);

    /* 相机(失败不阻断:UI/设置仍可用) */
    if (bsp_camera_init() != 0) {
        show_fatal("相机初始化失败!\n检查排线/供电");
    } else if (xTaskCreate(camera_task, "camera", 12288, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "采集任务创建失败");
    }

    xTaskCreate(status_task, "status", 4096, NULL, 2, NULL);
    xTaskCreate(net_task, "net", 6144, NULL, 3, NULL);

    ESP_LOGI(TAG, "启动完成");
}
