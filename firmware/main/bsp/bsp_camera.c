#include "bsp_camera.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp_pins.h"

static const char *TAG = "camera";

static bool s_inited = false;

int bsp_camera_init(void)
{
    camera_config_t cfg = {
        .pin_pwdn = -1,      /* 载板固定接地 */
        .pin_reset = -1,     /* 载板经 10k 上拉至 3.3V */
        .pin_xclk = -1,      /* ATK-MC2640 自带 24MHz 晶振,载板未连 XCLK */
        .pin_sccb_sda = PIN_CAM_SDA,
        .pin_sccb_scl = PIN_CAM_SCL,
        .pin_d7 = PIN_CAM_D7,
        .pin_d6 = PIN_CAM_D6,
        .pin_d5 = PIN_CAM_D5,
        .pin_d4 = PIN_CAM_D4,
        .pin_d3 = PIN_CAM_D3,
        .pin_d2 = PIN_CAM_D2,
        .pin_d1 = PIN_CAM_D1,
        .pin_d0 = PIN_CAM_D0,
        .pin_vsync = PIN_CAM_VSYNC,
        .pin_href = PIN_CAM_HREF,
        .pin_pclk = PIN_CAM_PCLK,
        /* 传感器时钟来自模块晶振;个别 esp32-camera 版本要求非零,
         * 若 init 报 XCLK 相关错误,改为 24000000(不影响外部晶振供电)。 */
        .xclk_freq_hz = 0,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_240X240,
        .grab_mode = CAMERA_GRAB_LATEST,
        .sccb_i2c_port = I2C_NUM_0,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        /* 备选:该版本驱动不接受 xclk_freq_hz=0 时用 24M 重试 */
        cfg.xclk_freq_hz = 24000000;
        err = esp_camera_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "相机初始化失败: %s(检查排线/供电/引脚)", esp_err_to_name(err));
            return -1;
        }
        ESP_LOGW(TAG, "相机以 xclk_freq_hz=24M 兼容模式初始化(模块自带晶振,忽略)");
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);          /* 按模组安装方向调整,实测如上下颠倒改 0 */
        s->set_hmirror(s, 0);
        /* 焊点场景:固定曝光更有利特征稳定,禁自动白平衡/自动增益波动 */
        s->set_whitebal(s, 0);
        s->set_awb_gain(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec_value(s, 300);    /* 手动偏暗曝光,减少焊面反光过曝 */
        s->set_gain_ctrl(s, 0);
        s->set_agc_gain(s, 0);
    }

    /* 自检:取一帧验证长度 */
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "取帧失败");
        return -1;
    }
    const size_t expect = (size_t)CAM_HRES * CAM_VRES * 2;
    if (fb->len != expect) {
        ESP_LOGE(TAG, "帧长度异常 %u != %u", (unsigned)fb->len, (unsigned)expect);
        esp_camera_fb_return(fb);
        return -1;
    }
    esp_camera_fb_return(fb);
    s_inited = true;
    ESP_LOGI(TAG, "OV2640 就绪: 240x240 RGB565");
    return 0;
}

camera_fb_t *bsp_camera_fb_get(TickType_t wait)
{
    (void)wait;   /* esp_camera_fb_get() 自身阻塞至新帧 */
    if (!s_inited) return NULL;
    return esp_camera_fb_get();
}

void bsp_camera_fb_return(camera_fb_t *fb)
{
    if (fb) esp_camera_fb_return(fb);
}
