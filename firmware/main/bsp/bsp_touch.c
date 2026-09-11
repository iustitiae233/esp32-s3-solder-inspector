#include "bsp_touch.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "app_nvs.h"
#include "bsp_pins.h"

static const char *TAG = "touch";

/* XPT2046 差分读命令(S=1, MODE=0 差分, PD=00) */
#define TP_CMD_X_POS 0xD0   /* X 位置 */
#define TP_CMD_Y_POS 0x90   /* Y 位置 */
#define TP_CMD_Z1    0xB0   /* Z1 压力 */

/* Z1 判定:未触摸≈4095,轻触≈400~900,重按<100 */
#define TP_Z1_THRESHOLD 1200

static spi_device_handle_t s_tp_spi;
static float s_calib[6];

static uint16_t tp_read_channel(uint8_t cmd)
{
    uint8_t tx[3] = { cmd, 0x00, 0x00 };
    uint8_t rx[3] = { 0 };
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    if (spi_device_polling_transmit(s_tp_spi, &t) != ESP_OK) return 0xFFF;
    return (uint16_t)(((rx[1] & 0x7F) << 5) | (rx[2] >> 3));
}

bool bsp_touch_read_raw(int *x_raw, int *y_raw)
{
    /* PENIRQ 低电平=有触摸;读 Z1 双确认防悬空噪声 */
    if (gpio_get_level(PIN_TP_IRQ) != 0) return false;
    uint16_t z1 = tp_read_channel(TP_CMD_Z1);
    if (z1 >= TP_Z1_THRESHOLD) return false;

    /* 两次采样取均值降噪 */
    uint16_t x1 = tp_read_channel(TP_CMD_X_POS);
    uint16_t y1 = tp_read_channel(TP_CMD_Y_POS);
    uint16_t x2 = tp_read_channel(TP_CMD_X_POS);
    uint16_t y2 = tp_read_channel(TP_CMD_Y_POS);
    if (x1 == 0xFFF || y1 == 0xFFF) return false;

    *x_raw = (x1 + x2) / 2;
    *y_raw = (y1 + y2) / 2;
    return true;
}

void bsp_touch_update_calib(void)
{
    app_nvs_get_calib(s_calib);
}

static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    int rx, ry;
    if (!bsp_touch_read_raw(&rx, &ry)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    float x = s_calib[0] * rx + s_calib[1] * ry + s_calib[2];
    float y = s_calib[3] * rx + s_calib[4] * ry + s_calib[5];
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > LCD_HRES - 1) x = LCD_HRES - 1;
    if (y > LCD_VRES - 1) y = LCD_VRES - 1;
    data->point.x = (int32_t)x;
    data->point.y = (int32_t)y;
    data->state = LV_INDEV_STATE_PRESSED;
}

int bsp_touch_init(void)
{
    /* PENIRQ 输入(低有效,模块自带上拉) */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_TP_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = TP_SPI_FREQ_HZ,   /* XPT2046 ≤ 2.5MHz */
        .mode = 0,
        .spics_io_num = PIN_TP_CS,
        .queue_size = 1,
    };
    if (spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_tp_spi) != ESP_OK) {
        ESP_LOGE(TAG, "触摸 SPI 设备注册失败");
        return -1;
    }

    bsp_touch_update_calib();

    lv_indev_t *indev = lv_indev_create(LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);
    ESP_LOGI(TAG, "XPT2046 触摸就绪");
    return 0;
}
