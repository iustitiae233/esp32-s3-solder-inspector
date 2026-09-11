#include "bsp_display.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bsp_pins.h"

static const char *TAG = "display";

/* ---------------- ILI9341 命令 ---------------- */
#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT  0x11
#define ILI9341_DISPOFF 0x28
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_RASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_MADCTL  0x36
#define ILI9341_PIXFMT  0x3A
#define ILI9341_INVON   0x21

static spi_device_handle_t s_lcd_spi;
static lv_display_t *s_disp;
static SemaphoreHandle_t s_lock;

/* ================= 低层 SPI 事务 ================= */

static void lcd_dc(int data_mode)
{
    gpio_set_level(PIN_LCD_DC, data_mode ? 1 : 0);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_dc(0);
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 8,
        .tx_data = { cmd },
    };
    spi_device_polling_transmit(s_lcd_spi, &t);
}

static void lcd_data(const uint8_t *data, int len)
{
    lcd_dc(1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_lcd_spi, &t);
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t b[4];
    lcd_cmd(ILI9341_CASET);
    b[0] = x0 >> 8; b[1] = x0; b[2] = x1 >> 8; b[3] = x1;
    lcd_data(b, 4);
    lcd_cmd(ILI9341_RASET);
    b[0] = y0 >> 8; b[1] = y0; b[2] = y1 >> 8; b[3] = y1;
    lcd_data(b, 4);
    lcd_cmd(ILI9341_RAMWR);
}

/* ================= ILI9341 初始化 ================= */

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} ili9341_init_t;

/* Adafruit 标准 ILI9341 上电序列 */
static const ili9341_init_t s_init_seq[] = {
    { 0xEF, {0x03, 0x80, 0x01}, 3, 0 },
    { 0xCF, {0x00, 0xC1, 0x30}, 3, 0 },
    { 0xED, {0x64, 0x03, 0x12, 0x81}, 4, 0 },
    { 0xE8, {0x85, 0x00, 0x78}, 3, 0 },
    { 0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0 },
    { 0xF7, {0x20}, 1, 0 },
    { 0xEA, {0x00, 0x00}, 2, 0 },
    { 0xC0, {0x23}, 1, 0 },              /* PWCTRL1 */
    { 0xC1, {0x10}, 1, 0 },              /* PWCTRL2 */
    { 0xC5, {0x3E, 0x28}, 2, 0 },        /* VMCTRL1 */
    { 0xC7, {0x86}, 1, 0 },              /* VMCTRL2 */
    /* MADCTL=0x00:竖屏 240x320。如实物上下颠倒改 0xC0,左右镜像改 0x40。 */
    { ILI9341_MADCTL, {0x00}, 1, 0 },
    { ILI9341_PIXFMT, {0x55}, 1, 0 },    /* RGB565 */
    { 0xB1, {0x00, 0x18}, 2, 0 },        /* FRMCTR1 */
    { 0xB6, {0x08, 0x82, 0x27}, 3, 0 },  /* DISCTRL */
    { 0xF2, {0x00}, 1, 0 },              /* 3G 关闭 */
    { 0x26, {0x01}, 1, 0 },              /* GAMMASET */
    { 0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
             0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15, 0 },
    { 0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
             0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15, 0 },
    { ILI9341_SLPOUT, {0}, 0, 120 },
    { ILI9341_INVON, {0}, 0, 0 },        /* MSP2807 IPS 面板需要反转 */
    { ILI9341_DISPON, {0}, 0, 20 },
};

static void ili9341_init(void)
{
    /* 硬件复位 */
    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(ILI9341_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(s_init_seq) / sizeof(s_init_seq[0]); i++) {
        const ili9341_init_t *st = &s_init_seq[i];
        lcd_cmd(st->cmd);
        if (st->len) lcd_data(st->data, st->len);
        if (st->delay_ms) vTaskDelay(pdMS_TO_TICKS(st->delay_ms));
    }
}

/* ================= LVGL 移植 ================= */

/* 40 行 × 240 列 RGB565,双缓冲,DMA 内存 */
#define FLUSH_LINES 40
static uint8_t s_buf1[LCD_HRES * FLUSH_LINES * 2];
static uint8_t s_buf2[LCD_HRES * FLUSH_LINES * 2];
static_assert(sizeof(s_buf1) == 19200, "buffer size");

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *src)
{
    int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    if (x2 < 0 || y2 < 0 || x1 >= LCD_HRES || y1 >= LCD_VRES) {
        lv_display_flush_ready(disp);
        return;
    }
    lcd_set_window(x1, y1, x2, y2);

    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;
    lcd_dc(1);
    if (w == LCD_HRES) {
        /* 全宽:整块一次发 */
        spi_transaction_t t = { .length = (size_t)w * h * 16, .tx_buffer = src };
        spi_device_polling_transmit(s_lcd_spi, &t);
    } else {
        /* 窄区:逐行发(RAMWR 后像素流按窗口自动换行) */
        for (int y = 0; y < h; y++) {
            spi_transaction_t t = {
                .length = (size_t)w * 16,
                .tx_buffer = src + (size_t)y * w * 2,
            };
            spi_device_polling_transmit(s_lcd_spi, &t);
        }
    }
    lv_display_flush_ready(disp);
}

static void tick_timer_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGive(s_lock);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ================= 公共接口 ================= */

int bsp_display_init(void)
{
    /* DC / RST 引脚 */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* SPI2 总线:MISO 接触摸 T_DO(LCD 只写不用 MISO) */
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_TP_MISO,
        .sclk_io_num = PIN_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_HRES * FLUSH_LINES * 2,
    };
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败: %s", esp_err_to_name(err));
        return -1;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode = 0,
        .spics_io_num = PIN_LCD_CS,
        .queue_size = 3,
    };
    err = spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_lcd_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI 设备注册失败: %s", esp_err_to_name(err));
        return -1;
    }

    ili9341_init();
    ESP_LOGI(TAG, "ILI9341 初始化完成");

    /* LVGL */
    lv_init();
    s_disp = lv_display_create(LCD_HRES, LCD_VRES);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, s_buf2, sizeof(s_buf1),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_lock = xSemaphoreCreateMutex();

    esp_timer_create_args_t tick_args = {
        .name = "lvgl_tick",
        .callback = tick_timer_cb,
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1 * 1000));

    if (xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "LVGL 任务创建失败");
        return -1;
    }
    return 0;
}

void bsp_display_lvgl_lock(void)
{
    if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

void bsp_display_lvgl_unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

lv_display_t *bsp_display_lvgl_disp(void)
{
    return s_disp;
}
