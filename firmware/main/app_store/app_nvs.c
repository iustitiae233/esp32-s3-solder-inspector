#include "app_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

static const char *TAG = "nvs";

#define NS_CFG "cfg"
#define NS_CNT "cnt"

#define K_SSID      "ssid"
#define K_PASS      "pass"
#define K_PC_IP     "pcip"
#define K_PC_PORT   "pcport"
#define K_THRESH    "thr"
#define K_BACKLIGHT "bl"
#define K_CALIB     "calib"

static nvs_handle_t s_hcfg;
static nvs_handle_t s_hcnt;
static SemaphoreHandle_t s_cnt_lock;   /* 计数读-改-写原子性 */

void app_nvs_open_all(void)
{
    if (nvs_open(NS_CFG, NVS_READWRITE, &s_hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "NVS cfg 打开失败");
    }
    if (nvs_open(NS_CNT, NVS_READWRITE, &s_hcnt) != ESP_OK) {
        ESP_LOGE(TAG, "NVS cnt 打开失败");
    }
    s_cnt_lock = xSemaphoreCreateMutex();
}

/* ---------- WiFi ---------- */

bool app_nvs_get_wifi(char *ssid, size_t n_ssid, char *pass, size_t n_pass)
{
    size_t len = n_ssid;
    esp_err_t e1 = nvs_get_str(s_hcfg, K_SSID, ssid, &len);
    len = n_pass;
    esp_err_t e2 = nvs_get_str(s_hcfg, K_PASS, pass, &len);
    return e1 == ESP_OK && e2 == ESP_OK;
}

void app_nvs_set_wifi(const char *ssid, const char *pass)
{
    nvs_set_str(s_hcfg, K_SSID, ssid);
    nvs_set_str(s_hcfg, K_PASS, pass);
    nvs_commit(s_hcfg);
}

/* ---------- PC 地址 ---------- */

void app_nvs_get_pc_addr(char *ip, size_t n_ip, uint16_t *port)
{
    size_t len = n_ip;
    if (nvs_get_str(s_hcfg, K_PC_IP, ip, &len) != ESP_OK) {
        strlcpy(ip, "192.168.1.100", n_ip);
    }
    int16_t p = 3333;
    nvs_get_i16(s_hcfg, K_PC_PORT, &p);
    *port = (uint16_t)p;
}

void app_nvs_set_pc_addr(const char *ip, uint16_t port)
{
    nvs_set_str(s_hcfg, K_PC_IP, ip);
    nvs_set_i16(s_hcfg, K_PC_PORT, (int16_t)port);
    nvs_commit(s_hcfg);
}

/* ---------- 阈值 / 背光 ---------- */

int app_nvs_get_threshold(void)
{
    int8_t v = 60;
    nvs_get_i8(s_hcfg, K_THRESH, &v);
    return v;
}

void app_nvs_set_threshold(int pct)
{
    if (pct < 1) pct = 1;
    if (pct > 99) pct = 99;
    nvs_set_i8(s_hcfg, K_THRESH, (int8_t)pct);
    nvs_commit(s_hcfg);
}

int app_nvs_get_backlight(void)
{
    int8_t v = 80;
    nvs_get_i8(s_hcfg, K_BACKLIGHT, &v);
    return v;
}

void app_nvs_set_backlight(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    nvs_set_i8(s_hcfg, K_BACKLIGHT, (int8_t)pct);
    nvs_commit(s_hcfg);
}

/* ---------- 触摸校准 ---------- */

void app_nvs_get_calib(float m[6])
{
    size_t len = sizeof(float) * 6;
    if (nvs_get_blob(s_hcfg, K_CALIB, m, &len) != ESP_OK) {
        /* 默认:典型 XPT2046 值域 x∈[200,3900] y∈[200,3800] 线性映射到 240x320 */
        m[0] = 240.0f / 3700.0f; m[1] = 0.0f; m[2] = -200.0f * 240.0f / 3700.0f;
        m[3] = 0.0f;             m[4] = 320.0f / 3600.0f; m[5] = -200.0f * 320.0f / 3600.0f;
    }
}

void app_nvs_set_calib(const float m[6])
{
    nvs_set_blob(s_hcfg, K_CALIB, m, sizeof(float) * 6);
    nvs_commit(s_hcfg);
}

/* ---------- 分类统计 ---------- */

uint32_t app_nvs_get_class_count(int cls)
{
    char key[4] = { 'c', '0' + (char)(cls / 10), '0' + (char)(cls % 10), 0 };
    uint32_t v = 0;
    nvs_get_u32(s_hcnt, key, &v);
    return v;
}

void app_nvs_add_class_count(int cls, uint32_t n)
{
    if (!s_cnt_lock) return;
    char key[4] = { 'c', '0' + (char)(cls / 10), '0' + (char)(cls % 10), 0 };
    xSemaphoreTake(s_cnt_lock, portMAX_DELAY);
    uint32_t v = 0;
    nvs_get_u32(s_hcnt, key, &v);
    nvs_set_u32(s_hcnt, key, v + n);
    nvs_commit(s_hcnt);
    xSemaphoreGive(s_cnt_lock);
}

void app_nvs_clear_class_counts(void)
{
    if (!s_cnt_lock) return;
    xSemaphoreTake(s_cnt_lock, portMAX_DELAY);
    for (int i = 0; i < 8; i++) {
        char key[4] = { 'c', '0' + (char)(i / 10), '0' + (char)(i % 10), 0 };
        nvs_set_u32(s_hcnt, key, 0);
    }
    nvs_commit(s_hcnt);
    xSemaphoreGive(s_cnt_lock);
}
