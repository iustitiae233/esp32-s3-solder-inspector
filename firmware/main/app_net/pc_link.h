#ifndef PC_LINK_H
#define PC_LINK_H

#include <stdbool.h>
#include <stdint.h>

#include "frame_hub.h"
#include "proto.h"

/**
 * @brief 与 PC 检测台的 TCP 链路(设备为客户端,断线 3s 自动重连)。
 */

/** 启动链路任务(连接 host:port)。 */
void pc_link_start(const char *host, uint16_t port);

/** 链路是否已建立。 */
bool pc_link_is_up(void);

/** 发送一帧图像(RGB565 240x240);阻塞至发送完成,失败返回 -1。 */
int pc_link_send_image(const frame_t *f);

/** 发送 HELLO(连接建立时自动发送)。 */
int pc_link_send_hello(void);

/** PC 检测结果回调(payload 为 DETECT 格式,运行在 pc_link 任务上下文)。 */
void pc_link_set_on_detect(void (*cb)(const uint8_t *payload, int len));

/** PC 命令回调(cmd 取 PROTO_CMD_*,arg 为参数)。 */
void pc_link_set_on_command(void (*cb)(uint8_t cmd, uint8_t arg));

/** 重配目标地址并重连(设置页保存后调用)。 */
void pc_link_retarget(const char *host, uint16_t port);

#endif /* PC_LINK_H */
