/**
 * @file proto.h
 * @brief 设备 ↔ PC 二进制协议(小端)。与 pc/proto.py 严格对应,修改须两侧同步。
 *
 * 帧结构: [u32 magic][u16 type][u16 len][payload]
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROTO_MAGIC           0xA55A1234UL

#define PROTO_TYPE_HELLO      0x01
#define PROTO_TYPE_IMAGE      0x02
#define PROTO_TYPE_DETECT     0x03
#define PROTO_TYPE_HEARTBEAT  0x04
#define PROTO_TYPE_COMMAND    0x05

/* HELLO payload: u8 ver, u8 rsv[3], char name[16], u32 fw_ver */
#define PROTO_HELLO_VERSION   1

/* IMAGE payload: u16 w, u16 h, u8 fmt, u8 rsv, u32 frame_id, pixels */
#define PROTO_IMG_FMT_RGB565LE 0
#define PROTO_IMG_HEAD_LEN    10

/* DETECT payload: u32 frame_id, u16 n, ×n{ f32 x, f32 y, f32 w, f32 h, u8 cls, u8 rsv, f32 conf }
 * 坐标为 240x240 原图像素坐标。 */
#define PROTO_DETECT_BOX_LEN  20

/* COMMAND payload: u8 cmd, u8 arg */
#define PROTO_CMD_STREAM_START 0x01
#define PROTO_CMD_STREAM_STOP  0x02
#define PROTO_CMD_SINGLE_SHOT  0x03

#define PROTO_FRAME_HEAD_LEN  8

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t type;
    uint16_t len;
} proto_hdr_t;

#endif /* PROTO_H */
