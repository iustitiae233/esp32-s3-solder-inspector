#include "bsp_key.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_pins.h"

static const char *TAG = "key";

#define DEBOUNCE_MS 20
#define LONG_PRESS_MS 800

static void (*s_cb)(int key, int event) = NULL;

static bool key_level(int key)
{
    int pin = (key == BSP_KEY_K1) ? PIN_KEY_K1 : PIN_KEY_K2;
    return gpio_get_level(pin) == 0;   /* 低电平=按下 */
}

bool bsp_key_is_pressed(int key)
{
    return key_level(key);
}

/* 20ms 轮询:与硬件 100nF 消抖配合(载板文档建议软件 20~50ms) */
static void key_task(void *arg)
{
    (void)arg;
    const int ids[2] = { BSP_KEY_K1, BSP_KEY_K2 };
    bool prev[2] = { false, false };
    TickType_t down_since[2] = { 0, 0 };
    bool long_fired[2] = { false, false };

    while (1) {
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < 2; i++) {
            bool down = key_level(ids[i]);
            if (down && !prev[i]) {
                down_since[i] = now;
                long_fired[i] = false;
            }
            if (down) {
                if (!long_fired[i] &&
                    (now - down_since[i]) >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                    long_fired[i] = true;
                    if (s_cb) s_cb(ids[i], BSP_KEY_EVENT_LONG_PRESS);
                }
            } else if (!down && prev[i]) {
                if (!long_fired[i] && s_cb) s_cb(ids[i], BSP_KEY_EVENT_PRESS);
            }
            prev[i] = down;
        }
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
    }
}

void bsp_key_init(void)
{
    const int pins[2] = { PIN_KEY_K1, PIN_KEY_K2 };
    for (int i = 0; i < 2; i++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,   /* 载板按键低有效 */
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io));
    }
    if (xTaskCreate(key_task, "key", 3072, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "按键任务创建失败");
    }
}

void bsp_key_set_cb(void (*on_key)(int key, int event))
{
    s_cb = on_key;
}
