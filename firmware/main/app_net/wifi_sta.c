#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_events;
static bool s_up = false;
static void (*s_on_up)(void) = NULL;
static void (*s_on_down)(void) = NULL;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_up) {
            s_up = false;
            if (s_on_down) s_on_down();
        }
        ESP_LOGW(TAG, "断开,自动重连...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "已连接, IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_up = true;
        if (s_events) xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        if (s_on_up) s_on_up();
    }
}

int wifi_sta_start(const char *ssid, const char *pass)
{
    static bool s_inited = false;
    if (!s_inited) {
        s_events = xEventGroupCreate();
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            event_handler, NULL, NULL);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        s_inited = true;
    }

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "连接 \"%s\" ...", ssid);
    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "连接超时(后台继续重连)");
        return -1;
    }
    return 0;
}

bool wifi_sta_is_up(void) { return s_up; }

void wifi_sta_get_ip(char *ip, size_t n)
{
    if (!s_up) {
        strlcpy(ip, "0.0.0.0", n);
        return;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        strlcpy(ip, "0.0.0.0", n);
        return;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK) {
        strlcpy(ip, "0.0.0.0", n);
        return;
    }
    snprintf(ip, n, IPSTR, IP2STR(&info.ip));
}

void wifi_sta_set_cb(void (*on_up)(void), void (*on_down)(void))
{
    s_on_up = on_up;
    s_on_down = on_down;
}
