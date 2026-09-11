#include "bsp_backlight.h"

#include "driver/ledc.h"
#include "esp_log.h"
#include "bsp_pins.h"

static const char *TAG = "backlight";
static int s_percent = 0;

#define BL_TIMER   LEDC_TIMER_0
#define BL_CHANNEL LEDC_CHANNEL_0

int bsp_backlight_init(void)
{
    ledc_timer_config_t tim = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_TIMER,
        .freq_hz = 5000,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tim));

    ledc_channel_config_t ch = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,                 /* 先灭,init 完成后由上层设置亮度 */
        .hpoint = 0,
        .flags.output_invert = 0,  /* S8050 高电平点亮,正逻辑 */
    };
    esp_err_t err = ledc_channel_config(&ch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "背光 PWM 初始化失败: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}

void bsp_backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * 1023 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
    s_percent = percent;
}

int bsp_backlight_get(void)
{
    return s_percent;
}
