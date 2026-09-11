#ifndef APP_NVS_H
#define APP_NVS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 配置与统计持久化(NVS)。
 * 命名空间:cfg(配置)/ cnt(分类计数)。
 * 需先于本模块执行 nvs_flash_init()(main.c 负责)。
 */

/** 打开两个命名空间的 NVS 句柄(nvs_flash_init 之后调用一次)。 */
void app_nvs_open_all(void);

/* ---------- WiFi ---------- */
bool app_nvs_get_wifi(char *ssid, size_t n_ssid, char *pass, size_t n_pass);
void app_nvs_set_wifi(const char *ssid, const char *pass);

/* ---------- PC 地址 ---------- */
/** 默认 "192.168.1.100" : 3333 */
void app_nvs_get_pc_addr(char *ip, size_t n_ip, uint16_t *port);
void app_nvs_set_pc_addr(const char *ip, uint16_t port);

/* ---------- 阈值 / 背光 ---------- */
int app_nvs_get_threshold(void);          /* 1~99,默认 60 */
void app_nvs_set_threshold(int pct);
int app_nvs_get_backlight(void);          /* 0~100,默认 80 */
void app_nvs_set_backlight(int pct);

/* ---------- 触摸校准 ---------- */
/** 校准矩阵 m:x_scr = m[0]*x_raw + m[1]*y_raw + m[2];y_scr = m[3]*x_raw + m[4]*y_raw + m[5]
 *  未校准时返回默认线性映射(典型 XPT2040 值域 200~3900)。 */
void app_nvs_get_calib(float m[6]);
void app_nvs_set_calib(const float m[6]);

/* ---------- 分类统计 ---------- */
uint32_t app_nvs_get_class_count(int cls);
void app_nvs_add_class_count(int cls, uint32_t n);
void app_nvs_clear_class_counts(void);

#endif /* APP_NVS_H */
