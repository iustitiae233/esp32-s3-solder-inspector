#ifndef EDGE_AI_H
#define EDGE_AI_H

#include <stdbool.h>

#include "frame_hub.h"

typedef struct {
    int   class_id;      /* 0..N-1 */
    float confidence;    /* softmax 最大概率 */
    int   inference_ms;  /* 本次推理耗时 */
} edge_result_t;

/**
 * @brief 从 /storage 加载 int8 模型与标签。
 *
 * 优先读取 /storage/model_int8.tflite 与 /storage/labels.txt;
 * 模型缺失时退回内置默认标签(仅用于界面显示,无法推理)。
 * @return 0 模型加载成功并可用于推理;-1 无可用模型(调用 edge_ai_ready 为 false)
 */
int edge_ai_load(void);

/** 模型是否就绪(未就绪时 UI 禁用边缘模式)。 */
bool edge_ai_ready(void);

/** 类别数。 */
int edge_ai_class_count(void);

/** 类别名(UTF-8 中文);越界返回 "?"。 */
const char *edge_ai_label(int i);

/**
 * @brief 对一帧执行分类(RGB565 240x240 → 96x96 int8 → 推理 → softmax)。
 * 阻塞约 100~400ms,运行在调用者任务上下文。
 * @return 0 成功;-1 未就绪或推理失败
 */
int edge_ai_classify(const frame_t *f, edge_result_t *out);

#endif /* EDGE_AI_H */
