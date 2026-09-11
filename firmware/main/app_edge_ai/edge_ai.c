#include "edge_ai.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "bsp_pins.h"

/* 内置默认标签(EMBED_TXTFILES labels_default.txt) */
extern const char _binary_labels_default_txt_start[] asm("_binary_labels_default_txt_start");
extern const char _binary_labels_default_txt_end[] asm("_binary_labels_default_txt_end");

static const char *TAG = "edgeai";

#define EDGE_INPUT_SIZE 96
#define EDGE_ARENA_SIZE (200 * 1024)

#define MAX_CLASSES 8
#define LABEL_LEN   24

static const tflite::Model *s_model = nullptr;
static uint8_t *s_model_buf = nullptr;      /* PSRAM */
static uint8_t *s_arena = nullptr;          /* PSRAM */
static tflite::MicroInterpreter *s_interp = nullptr;
static TfLiteTensor *s_in = nullptr;
static TfLiteTensor *s_out = nullptr;

static char s_labels[MAX_CLASSES][LABEL_LEN];
static int  s_nlabels = 0;
static bool s_ready = false;

/* ---------------- 标签 ---------------- */

static int load_labels_from_default(void)
{
    s_nlabels = 0;
    const char *p = _binary_labels_default_txt_start;
    const char *end = _binary_labels_default_txt_end;
    while (p < end && s_nlabels < MAX_CLASSES) {
        const char *nl = (const char *)memchr(p, '\n', end - p);
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 0 && len < LABEL_LEN) {
            memcpy(s_labels[s_nlabels], p, len);
            s_labels[s_nlabels][len] = 0;
            s_nlabels++;
        }
        if (!nl) break;
        p = nl + 1;
    }
    return s_nlabels;
}

static int load_labels(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return load_labels_from_default();

    s_nlabels = 0;
    char line[LABEL_LEN];
    while (s_nlabels < MAX_CLASSES && fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len == 0) continue;
        strlcpy(s_labels[s_nlabels], line, LABEL_LEN);
        s_nlabels++;
    }
    fclose(fp);
    return s_nlabels;
}

/* ---------------- 加载模型 ---------------- */

int edge_ai_load(void)
{
    FILE *fp = fopen("/storage/model_int8.tflite", "rb");
    if (fp == NULL) {
        ESP_LOGW(TAG, "未找到 /storage/model_int8.tflite,边缘推理不可用(可用 flash_model.py 烧录)");
        load_labels(NULL);
        s_ready = false;
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(fp);
        ESP_LOGE(TAG, "模型文件大小异常 %ld", sz);
        return -1;
    }
    s_model_buf = (uint8_t *)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (s_model_buf == NULL || fread(s_model_buf, 1, sz, fp) != (size_t)sz) {
        fclose(fp);
        ESP_LOGE(TAG, "模型读取失败");
        return -1;
    }
    fclose(fp);

    s_model = tflite::GetModel(s_model_buf);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "模型 schema 版本不匹配: %lu", (unsigned long)s_model->version());
        return -1;
    }

    /* MobileNetV2 int8 需要的算子集 */
    static tflite::MicroMutableOpResolver<12> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddFullyConnected();
    resolver.AddAveragePool2D();
    resolver.AddMaxPool2D();
    resolver.AddReshape();
    resolver.AddSoftmax();
    resolver.AddRelu();
    resolver.AddRelu6();
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddConcatenation();

    s_arena = (uint8_t *)heap_caps_malloc(EDGE_ARENA_SIZE, MALLOC_CAP_SPIRAM);
    if (s_arena == NULL) {
        ESP_LOGE(TAG, "arena 分配失败");
        return -1;
    }
    static tflite::MicroInterpreter interp(s_model, resolver, s_arena, EDGE_ARENA_SIZE);
    s_interp = &interp;

    TfLiteStatus st = s_interp->AllocateTensors();
    if (st != kTfLiteOk) {
        ESP_LOGE(TAG, "AllocateTensors 失败(arena 不足或算子缺失)");
        s_interp = nullptr;
        return -1;
    }
    s_in = s_interp->input(0);
    s_out = s_interp->output(0);
    if (s_in->dims->size != 4 || s_in->dims->data[1] != EDGE_INPUT_SIZE ||
        s_in->dims->data[2] != EDGE_INPUT_SIZE || s_in->type != kTfLiteInt8) {
        ESP_LOGE(TAG, "模型输入不是 [1,%d,%d,3] int8", EDGE_INPUT_SIZE, EDGE_INPUT_SIZE);
        return -1;
    }

    int nl = load_labels("/storage/labels.txt");
    int ncls = s_out->dims->data[s_out->dims->size - 1];
    ESP_LOGI(TAG, "模型就绪: 输入[%d,%d,%d] 类别数 %d 标签 %d 个",
             s_in->dims->data[1], s_in->dims->data[2], s_in->dims->data[3], ncls, nl);
    if (nl != ncls) {
        ESP_LOGW(TAG, "标签数(%d)与模型类别数(%d)不一致,显示将用序号", nl, ncls);
    }
    s_ready = true;
    return 0;
}

bool edge_ai_ready(void) { return s_ready; }

int edge_ai_class_count(void) { return s_nlabels; }

const char *edge_ai_label(int i)
{
    if (i < 0 || i >= s_nlabels) return "?";
    return s_labels[i];
}

/* ---------------- 推理 ---------------- */

static inline int8_t float_to_int8(float f, float scale, int32_t zp)
{
    int q = (int)lroundf(f / scale) + zp;
    if (q > 127) q = 127;
    if (q < -128) q = -128;
    return (int8_t)q;
}

int edge_ai_classify(const frame_t *f, edge_result_t *out)
{
    if (!s_ready || s_interp == nullptr || f == nullptr || out == nullptr) return -1;

    int64_t t0 = esp_timer_get_time();

    /* RGB565 小端 240x240 → 96x96 int8(最近邻 + [-1,1] 量化,与训练脚本一致) */
    const uint16_t *src = (const uint16_t *)f->buf;
    int8_t *dst = s_in->data.int8;
    const float in_scale = s_in->params.scale;
    const int32_t in_zp = s_in->params.zero_point;

    for (int dy = 0; dy < EDGE_INPUT_SIZE; dy++) {
        int sy = (dy * CAM_VRES + CAM_VRES / 2) / EDGE_INPUT_SIZE;
        const uint16_t *srow = src + (size_t)sy * CAM_HRES;
        for (int dx = 0; dx < EDGE_INPUT_SIZE; dx++) {
            int sx = (dx * CAM_HRES + CAM_HRES / 2) / EDGE_INPUT_SIZE;
            uint16_t px = srow[sx];
            float r = (float)(((px >> 11) & 0x1F) * 255 / 31) / 127.5f - 1.0f;
            float g = (float)(((px >> 5) & 0x3F) * 255 / 63) / 127.5f - 1.0f;
            float b = (float)((px & 0x1F) * 255 / 31) / 127.5f - 1.0f;
            *dst++ = float_to_int8(r, in_scale, in_zp);
            *dst++ = float_to_int8(g, in_scale, in_zp);
            *dst++ = float_to_int8(b, in_scale, in_zp);
        }
    }

    if (s_interp->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "Invoke 失败");
        return -1;
    }

    /* int8 logits → float → softmax */
    int n = s_out->dims->data[s_out->dims->size - 1];
    float logits[MAX_CLASSES];
    float maxv = -1e30f;
    for (int i = 0; i < n && i < MAX_CLASSES; i++) {
        logits[i] = ((float)s_out->data.int8[i] - (float)s_out->params.zero_point)
                    * s_out->params.scale;
        if (logits[i] > maxv) maxv = logits[i];
    }
    float sum = 0.0f;
    for (int i = 0; i < n && i < MAX_CLASSES; i++) {
        logits[i] = expf(logits[i] - maxv);
        sum += logits[i];
    }
    int best = 0;
    float best_p = 0.0f;
    for (int i = 0; i < n && i < MAX_CLASSES; i++) {
        logits[i] /= sum;
        if (logits[i] > best_p) { best_p = logits[i]; best = i; }
    }

    out->class_id = best;
    out->confidence = best_p;
    out->inference_ms = (int)((esp_timer_get_time() - t0) / 1000);
    return 0;
}
