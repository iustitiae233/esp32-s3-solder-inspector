#include "frame_hub.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "framehub";

static frame_cb_t s_cbs[8];
static volatile uint32_t s_mask = 0;

void frame_hub_on_frame(uint32_t sub, frame_cb_t cb)
{
    int idx = __builtin_ctz(sub);   /* 最低置位位即数组下标 */
    if (idx < 0 || idx >= 8) {
        ESP_LOGE(TAG, "非法订阅位 0x%x", sub);
        return;
    }
    s_cbs[idx] = cb;
}

void frame_hub_set_mask(uint32_t mask)
{
    mask &= FH_SUB_ALL;
    s_mask = mask;
    ESP_LOGI(TAG, "订阅掩码 -> 0x%x (显示%d 边缘%d 网络%d)", mask,
             !!(mask & FH_SUB_DISPLAY), !!(mask & FH_SUB_EDGE_AI), !!(mask & FH_SUB_NET));
}

uint32_t frame_hub_get_mask(void)
{
    return s_mask;
}

void frame_hub_dispatch_masked(uint32_t mask, const frame_t *f)
{
    if ((mask & FH_SUB_DISPLAY) && s_cbs[0]) s_cbs[0](f);
    if ((mask & FH_SUB_EDGE_AI) && s_cbs[1]) s_cbs[1](f);
    if ((mask & FH_SUB_NET) && s_cbs[2])     s_cbs[2](f);
}

void frame_hub_dispatch(const frame_t *f)
{
    frame_hub_dispatch_masked(s_mask, f);
}
