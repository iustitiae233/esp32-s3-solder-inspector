#include "pc_link.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "bsp_pins.h"

static const char *TAG = "pclink";

#define RX_BUF_LEN   16384
#define MAX_PAYLOAD  8192        /* 设备只收检测框/命令/心跳,无需大帧 */
#define RECONNECT_MS 3000
#define HEARTBEAT_MS 2000

static char s_host[64] = "";
static uint16_t s_port = 3333;
static volatile int s_sock = -1;
static volatile bool s_up = false;
static volatile bool s_retarget = false;
static SemaphoreHandle_t s_tx_lock;
static void (*s_on_detect)(const uint8_t *, int) = NULL;
static void (*s_on_command)(uint8_t, uint8_t) = NULL;

/* ---------------- 发送 ---------------- */

static int send_all(int sock, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        int n = send(sock, p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int send_frame(uint16_t type, const void *payload, uint16_t len)
{
    int sock = s_sock;
    if (sock < 0) return -1;
    proto_hdr_t hdr = { .magic = PROTO_MAGIC, .type = type, .len = len };
    if (send_all(sock, &hdr, sizeof(hdr)) != 0) return -1;
    if (len > 0 && send_all(sock, payload, len) != 0) return -1;
    return 0;
}

int pc_link_send_image(const frame_t *f)
{
    if (!s_up || f == NULL) return -1;
    uint8_t head[PROTO_IMG_HEAD_LEN];
    head[0] = CAM_HRES & 0xFF;  head[1] = CAM_HRES >> 8;
    head[2] = CAM_VRES & 0xFF;  head[3] = CAM_VRES >> 8;
    head[4] = PROTO_IMG_FMT_RGB565LE;
    head[5] = 0;
    head[6] = f->frame_id & 0xFF;  head[7] = (f->frame_id >> 8) & 0xFF;
    head[8] = (f->frame_id >> 16) & 0xFF; head[9] = (f->frame_id >> 24) & 0xFF;

    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    int rc = send_frame(PROTO_TYPE_IMAGE, head, sizeof(head));
    if (rc == 0) {
        rc = send_all(s_sock, f->buf, (size_t)CAM_HRES * CAM_VRES * 2);
    }
    xSemaphoreGive(s_tx_lock);
    if (rc != 0) ESP_LOGW(TAG, "图像发送失败");
    return rc;
}

int pc_link_send_hello(void)
{
    uint8_t pl[24];
    memset(pl, 0, sizeof(pl));
    pl[0] = PROTO_HELLO_VERSION;
    memcpy(pl + 4, "PCB-Inspector", 14);
    pl[4 + 13] = 0;
    uint32_t fw = 0x00010000;   /* v1.0.0 */
    memcpy(pl + 20, &fw, 4);
    return send_frame(PROTO_TYPE_HELLO, pl, sizeof(pl));
}

/* ---------------- 接收解析(流式 + magic 重同步) ---------------- */

static uint8_t s_rx[RX_BUF_LEN];
static size_t s_rx_len = 0;

static void handle_payload(uint16_t type, const uint8_t *pl, uint16_t len)
{
    if (type == PROTO_TYPE_DETECT && s_on_detect && len >= 6) {
        s_on_detect(pl, len);
    } else if (type == PROTO_TYPE_COMMAND && s_on_command && len >= 2) {
        s_on_command(pl[0], pl[1]);
    }
    /* HEARTBEAT / HELLO:无需处理 */
}

static void process_rx(void)
{
    for (;;) {
        if (s_rx_len < PROTO_FRAME_HEAD_LEN) return;
        proto_hdr_t hdr;
        memcpy(&hdr, s_rx, sizeof(hdr));
        if (hdr.magic != PROTO_MAGIC) {
            memmove(s_rx, s_rx + 1, s_rx_len - 1);
            s_rx_len--;
            continue;
        }
        if (hdr.len > MAX_PAYLOAD) {   /* 非法帧,整块丢弃重新同步 */
            s_rx_len = 0;
            return;
        }
        if (s_rx_len < (size_t)PROTO_FRAME_HEAD_LEN + hdr.len) return;
        handle_payload(hdr.type, s_rx + PROTO_FRAME_HEAD_LEN, hdr.len);
        size_t consumed = (size_t)PROTO_FRAME_HEAD_LEN + hdr.len;
        memmove(s_rx, s_rx + consumed, s_rx_len - consumed);
        s_rx_len -= consumed;
    }
}

/* ---------------- 链路任务:连接→收发→心跳→重连 ---------------- */

static int try_connect(void)
{
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(s_port);
    if (inet_aton(s_host, &dst.sin_addr) != 1) {
        ESP_LOGE(TAG, "非法 PC 地址: %s", s_host);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    /* 3s 连接超时(非阻塞 connect + select) */
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(sock, (struct sockaddr *)&dst, sizeof(dst));
    if (rc != 0) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        rc = select(sock + 1, NULL, &wset, NULL, &tv);
        if (rc <= 0) {
            close(sock);
            return -1;
        }
        int err = 0;
        socklen_t elen = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) {
            close(sock);
            return -1;
        }
    }
    fcntl(sock, F_SETFL, flags);   /* 恢复阻塞 */

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return sock;
}

static void link_task(void *arg)
{
    (void)arg;
    uint32_t last_hb = 0;

    while (1) {
        if (s_retarget) {
            s_retarget = false;
            if (s_sock >= 0) { close(s_sock); s_sock = -1; }
            s_up = false;
        }
        s_rx_len = 0;
        last_hb = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        int sock = try_connect();
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(RECONNECT_MS));
            continue;
        }
        s_sock = sock;
        s_up = true;
        ESP_LOGI(TAG, "已连接 PC %s:%u", s_host, s_port);
        pc_link_send_hello();

        while (1) {
            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(sock, &rset);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
            int rc = select(sock + 1, &rset, NULL, NULL, &tv);
            if (rc < 0) break;

            if (rc > 0 && FD_ISSET(sock, &rset)) {
                int n = recv(sock, s_rx + s_rx_len, RX_BUF_LEN - s_rx_len, 0);
                if (n <= 0) break;             /* 对端关闭 */
                s_rx_len += (size_t)n;
                process_rx();
            }

            uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (now - last_hb >= HEARTBEAT_MS) {
                uint32_t ts = now;
                if (send_frame(PROTO_TYPE_HEARTBEAT, &ts, sizeof(ts)) != 0) break;
                last_hb = now;
            }
        }

        s_up = false;
        close(sock);
        s_sock = -1;
        ESP_LOGW(TAG, "PC 断开,%ums 后重连", RECONNECT_MS);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_MS));
    }
}

/* ---------------- 公共接口 ---------------- */

void pc_link_start(const char *host, uint16_t port)
{
    strlcpy(s_host, host, sizeof(s_host));
    s_port = port;
    if (s_tx_lock == NULL) s_tx_lock = xSemaphoreCreateMutex();
    if (xTaskCreate(link_task, "pclink", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "链路任务创建失败");
    }
}

bool pc_link_is_up(void) { return s_up; }

void pc_link_set_on_detect(void (*cb)(const uint8_t *payload, int len))
{
    s_on_detect = cb;
}

void pc_link_set_on_command(void (*cb)(uint8_t cmd, uint8_t arg))
{
    s_on_command = cb;
}

void pc_link_retarget(const char *host, uint16_t port)
{
    strlcpy(s_host, host, sizeof(s_host));
    s_port = port;
    s_retarget = true;
}
