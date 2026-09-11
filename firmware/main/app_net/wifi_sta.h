#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief WiFi STA 连接管理(阻塞式连接 + 断线自动重连)。
 * 需先初始化 NVS 与网络协议栈(main.c 负责 esp_netif_init/esp_event_loop_create)。
 */

/** 阻塞连接:成功获取 IP 返回 0;15s 超时返回 -1(后台继续重连)。 */
int wifi_sta_start(const char *ssid, const char *pass);

/** 当前是否已连接并获得 IP。 */
bool wifi_sta_is_up(void);

/** 获取 IP 字符串(未连接返回 "0.0.0.0")。 */
void wifi_sta_get_ip(char *ip, size_t n);

/** 连接状态回调(在 WiFi 事件任务上下文触发,勿阻塞)。 */
void wifi_sta_set_cb(void (*on_up)(void), void (*on_down)(void));

#endif /* WIFI_STA_H */
