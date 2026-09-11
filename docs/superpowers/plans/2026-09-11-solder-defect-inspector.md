# 焊点缺陷检测仪软件系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 YD-ESP32-S3-N16R8 载板上实现"LVGL 触摸界面 + OV2640 视觉 + TFLM 边缘分类 + PC(YOLOv8)远程检测"的焊点缺陷检测仪,含从零数据采集到模型烧录的完整工具链。

**Architecture:** 单一 RGB565 240×240 帧格式贯穿全系统;固件内帧分发器(发布-订阅)把相机帧按模式分发给显示/边缘推理/WiFi 发送;PC 端为 TCP Server 接收图像跑 YOLOv8 并回传检测框。

**Tech Stack:** ESP-IDF ≥5.2、LVGL ^9.3(含 CJK 字体)、espressif/esp32-camera ^2、espressif/esp-tflite-micro ^1、Python3.10+、PySide6、Ultralytics YOLOv8、TensorFlow/Keras。

**Spec:** `docs/superpowers/specs/2026-09-11-solder-defect-inspector-software-design.md`

## Global Constraints

- 目标芯片 ESP32-S3,16MB Flash,8MB **Octal PSRAM**(GPIO35/36/37 不可用);strapping GPIO0/3/45/46 不外接(载板 v1.2 已保证)。
- GPIO 分配严格按 spec §2 表(与载板 v1.2 一致),固件中集中定义于 `bsp_pins.h`,禁止散落魔法数字。
- LCD 与 XPT2046 共享 SPI2 总线:LCD 时钟 40MHz、触摸 2MHz;`CONFIG_LV_COLOR_16_SW_SWAP=y`(ILI9341 高字节在前)。
- 相机自带 24MHz 晶振:`xclk_pin = -1`;PWDN/RST/FLASH 由硬件固定电平,固件不控制。
- 协议:`[u32 magic 0xA55A1234][u16 type][u16 len][payload]` 小端;PC 监听端口 3333。
- 类别顺序(标签表由 labels.txt 决定,训练脚本与文档统一):`ok, excess, insufficient, bridge, misalignment, cold_joint`。
- TFLM tensor arena 200KB,从 PSRAM 分配;相机 `fb_count=2` 放 PSRAM。
- 命名:固件模块前缀 `bsp_ / app_ / ui_`,C 标识符英文,注释与 UI 文案中文。
- 每个任务完成后 `git commit`;固件以 `idf.py build` 零错误零警告为验证基线(无真机 TDD 条件),纯逻辑模块提供可运行测试;最终真机验收走 spec §11 清单。
- 分区表(partitions.csv,16MB):

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0xA000,
phy_init, data, phy,     0x13000,  0x1000,
factory,  app,  factory, 0x20000,  0x300000,
storage,  data, fat,     0x320000, 0x400000,
```

- `/storage` FATFS 内容:`model_int8.tflite`、`labels.txt`、`captures/`。

---

### Task 1: 固件工程骨架

**Files:**
- Create: `firmware/CMakeLists.txt`
- Create: `firmware/partitions.csv`(内容见全局约束)
- Create: `firmware/sdkconfig.defaults`
- Create: `firmware/main/CMakeLists.txt`
- Create: `firmware/main/idf_component.yml`
- Create: `firmware/main/bsp/bsp_pins.h`
- Create: `firmware/main/main.c`

**Interfaces (Produces):**
- `bsp_pins.h`:全部 GPIO 宏(`PIN_CAM_D0`=11 … 按 spec §2)+ 外设参数宏(`LCD_SPI_HOST`=SPI2_HOST, `LCD_HRES`=240, `LCD_VRES`=320, `CAM_HRES`=240, `CAM_VRES`=240)。
- `main.c`:启动骨架,仅 `app_main` 打印版本并空转(后续任务填调用)。

**Steps:**

- [ ] **Step 1.1** 写 `firmware/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(solder_inspector)
```

`sdkconfig.defaults`:

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=65536
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_FREERTOS_HZ=1000
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
# LVGL
CONFIG_LV_COLOR_16_SW_SWAP=y
CONFIG_LV_FONT_SIMSUN_16_CJK=y
CONFIG_LV_MEM_SIZE_KILOBYTES=96
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_MEM_CUSTOM_INCLUDE="esp_heap_caps.h"
```

注:LVGL 堆用 `heap_caps_malloc` 默认实现(SRAM 优先);`LV_COLOR_16_SW_SWAP` 保证 ILI9341 字节序。

`main/idf_component.yml`:

```yaml
dependencies:
  idf: ">=5.2"
  lvgl/lvgl: "^9.3.0"
  espressif/esp32-camera: "^2.0.0"
  espressif/esp-tflite-micro: "^1.1.0"
```

`main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
         "bsp/bsp_display.c" "bsp/bsp_touch.c" "bsp/bsp_camera.c"
         "bsp/bsp_key.c" "bsp/bsp_backlight.c"
         "app_camera/frame_hub.c"
         "app_edge_ai/edge_ai.c"
         "app_net/wifi_sta.c" "app_net/pc_link.c"
         "app_store/app_nvs.c"
         "ui/ui.c" "ui/ui_theme.c" "ui/ui_page_home.c" "ui/ui_page_detect.c"
         "ui/ui_page_capture.c" "ui/ui_page_stats.c" "ui/ui_page_settings.c"
         "ui/ui_calib.c"
    INCLUDE_DIRS "." "bsp" "app_camera" "app_edge_ai" "app_net" "app_store" "ui"
    EMBED_TXTFILES "labels_default.txt")
```

后续任务逐个补齐源文件;本任务先建空实现使编译通过。`labels_default.txt` 内置默认 6 行标签。

- [ ] **Step 1.2** 写 `bsp_pins.h`(GPIO 按 spec §2 全表)+ `main.c` 骨架(`app_main` 打印 banner、延迟循环)。
- [ ] **Step 1.3** 为 SRCS 里除 `main.c` 外的每个文件创建**空实现占位**(仅 include 自己头文件 + 空函数),头文件先只含 include guard——本任务的占位将在各自任务里被真实实现整体替换,不是最终代码。
- [ ] **Step 1.4** 验证:`idf.py build` 零错误(本机无 IDF 时跳过,记入 Task 9 统一验证)。
- [ ] **Step 1.5** Commit:`feat(fw): 工程骨架/组件依赖/分区表/引脚定义`

### Task 2: 显示 + 背光(ILI9341 + LVGL 移植)

**Files:**
- Modify: `firmware/main/bsp/bsp_display.c`、`firmware/main/bsp/bsp_backlight.c`(替换占位)
- Create: `firmware/main/bsp/bsp_display.h`、`firmware/main/bsp/bsp_backlight.h`

**Interfaces:**
- Consumes: `bsp_pins.h`(LCD_* 宏)。
- Produces:
  - `int bsp_display_init(void)`;`void bsp_display_lvgl_lock(void)` / `void bsp_display_lvgl_unlock(void)`(封装 `lv_lock/lv_unlock`);`lv_display_t *bsp_display_lvgl_disp(void)`。
  - `int bsp_backlight_init(void)`;`void bsp_backlight_set(int percent)`(0~100,PWM LEDC 5kHz GPIO21)。

**核心实现要点(必须遵守):**

- SPI 总线:host=SPI2_HOST,`mosi_io_num=PIN_LCD_MOSI(41)`,`sclk_io_num=PIN_LCD_SCLK(40)`,`miso_io_num=-1`,`max_transfer_sz=LCD_HRES*40*2`(40 行块)。
- LCD 设备:`spi_bus_add_device`,clock 40MHz,`queue_size=2`,CS=42。
- ILI9341 初始化序列(SWRESET→SLPOUT→delay 120ms→`0x36 MADCTL=0x00` 竖屏→`0x3A COLMOD=0x55`(16bit)→`0xB6` 显示窗口参数→`0x21 INVON`(MSP2807 需要反转)→`0x11 SLPOUT`→`0x29 DISPON`),命令用轮询事务,DC=38 低=命令/高=数据。
- 窗口写函数:`static void ili9341_set_window(x0,y0,x1,y1)` 发 CASET(0x2A)/RASET(0x2B)/RAMWR(0x2C)。
- LVGL 显示端口(LVGL 9 API,禁止 v8 API):

```c
static lv_display_t *s_disp;
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    /* 全宽区域一次 DMA,否则逐行;完成后在 DMA 完成回调里调 lv_display_flush_ready */
}
```
双缓冲:`lv_display_set_buffers(disp, buf1, buf2, 240*40*2, LV_DISPLAY_RENDER_MODE_PARTIAL)`,buf 用内部 SRAM `heap_caps_malloc(..., MALLOC_CAP_DMA)`。DMA 异步:`spi_device_queue_trans` + 完成回调 `spi_transaction_t` 用户字段标记,`lv_display_flush_is_last` 判定后 ready。
- LVGL tick:`lv_init()` 后用 `esp_timer` 1ms 周期回调 `lv_tick_inc(1)`;独立 FreeRTOS 任务(栈 8KB,优先级 4)循环 `lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(10))`。

**Steps:**

- [ ] **Step 2.1** 实现 `bsp_backlight.c`(LEDC 通道 0,5kHz,占空比=percent)。
- [ ] **Step 2.2** 实现 `bsp_display.c`:SPI 总线 + ILI9341 init + LVGL port + LVGL 任务(代码要点见上,逐条实现)。
- [ ] **Step 2.3** `main.c` 调用:`bsp_backlight_init(); bsp_backlight_set(80); bsp_display_init();`,建临时测试标签(`lv_label_create(lv_screen_active())` 显示 "PCB Inspector")验证 LVGL 走通(真机)。
- [ ] **Step 2.4** 验证:`idf.py build` 零错误;真机(若有)点亮屏显示文字。
- [ ] **Step 2.5** Commit:`feat(fw): ILI9341+LVGL9 移植(共享SPI/双缓冲DMA/背光PWM)`

### Task 3: 触摸(XPT2046)+ 校准 + NVS 存储

**Files:**
- Modify: `firmware/main/bsp/bsp_touch.c`、`firmware/main/app_store/app_nvs.c`
- Create: `firmware/main/bsp/bsp_touch.h`、`firmware/main/app_store/app_nvs.h`、`firmware/main/ui/ui_calib.c`(校准逻辑,UI 交互在 Task 6 完整化)

**Interfaces:**
- Consumes: SPI2 总线(Task 2 已建);`bsp_display_lvgl_lock/unlock`。
- Produces:
  - `int bsp_touch_init(void)`;`bool bsp_touch_read_raw(int *x, int *y)`(原始 12bit);LVGL pointer indev 已注册。
  - `app_nvs.h`:
    - `void app_nvs_init(void);`
    - `bool app_nvs_get_wifi(char *ssid, size_t n_ssid, char *pass, size_t n_pass);`
    - `void app_nvs_set_wifi(const char *ssid, const char *pass);`
    - `void app_nvs_get_pc_addr(char *ip, size_t n_ip, uint16_t *port);`(默认 `192.168.1.100:3333`)
    - `void app_nvs_set_pc_addr(const char *ip, uint16_t port);`
    - `int app_nvs_get_threshold(void);` / `set_threshold(int pct)`(默认 60)
    - `int app_nvs_get_backlight(void);` / `set_backlight(int pct)`(默认 80)
    - `void app_nvs_get_calib(float m[6]);` / `set_calib(const float m[6]);`(默认单位映射)
    - `uint32_t app_nvs_get_class_count(int cls);` / `app_nvs_add_class_count(int cls, uint32_t n);` / `app_nvs_clear_class_counts(void);`(命名空间 `cnt`,key `c0..c7`)
- 校准数据:`x_scr = m[0]*x_raw + m[1]*y_raw + m[2]; y_scr = m[3]*x_raw + m[4]*y_raw + m[5]`(两点校准解出 m)。

**核心实现要点:**

- XPT2046 事务(软件拼接 3 字节命令应答,时钟 2MHz,挂 SPI2 总线,CS=2):

```c
#define CMD_X_READ  0x90   /* 差分读 X */
#define CMD_Y_READ  0xD0   /* 差分读 Y */
/* 一次事务发 3 命令字节流:X/Y/Z1,一次往返读完(标准一次传输读三轴) */
static uint16_t tp_swap(uint8_t a, uint8_t b) { return ((uint16_t)(a & 0x7F) << 5) | (b >> 3); }
```
压力判断:Z1 > 阈值(经验 ≥ 50)才算按下;`T_IRQ`(GPIO47)低有效可作快速判定。
- indev read_cb(LVGL 9):`lv_indev_t *indev = lv_indev_create(LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(indev, read_cb);` read_cb 里 `_lv_indev_data_set_state(data, pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED); _lv_indev_data_set_point(data, &pt);` 坐标经校准矩阵映射并按屏方向调整。
- 默认校准系数:典型 XPT2046 原始值域 x∈[200,3900]、y∈[200,3800] 线性映射到 [0,240)×[0,320)。

**Steps:**

- [ ] **Step 3.1** 实现 `app_nvs.c` 全部 get/set(直接 `nvs_get_i32/str/blob`,key 常量集中定义)。
- [ ] **Step 3.2** 实现 `bsp_touch.c`:XPT2046 读取 + 压力判断 + 校准映射 + LVGL indev。
- [ ] **Step 3.3** 实现 `ui_calib.c` 校准核心:`ui_calib_collect(x_raw,y_raw,corner)` 收集左上(12,12)/右下(228,308)两点,解 2×3 线性方程组,`app_nvs_set_calib` 持久化。
- [ ] **Step 3.4** 验证:`idf.py build`;真机(若有)触摸拖动/点击。
- [ ] **Step 3.5** Commit:`feat(fw): XPT2046触摸+两点校准+NVS配置存储`

### Task 4: 相机 + 帧分发器 + 按键

**Files:**
- Modify: `firmware/main/bsp/bsp_camera.c`、`firmware/main/app_camera/frame_hub.c`、`firmware/main/bsp/bsp_key.c`
- Create: `firmware/main/bsp/bsp_camera.h`、`firmware/main/app_camera/frame_hub.h`、`firmware/main/bsp/bsp_key.h`

**Interfaces:**
- Consumes: `bsp_pins.h` CAM_* 宏。
- Produces:
  - `int bsp_camera_init(void)`;`camera_fb_t *bsp_camera_fb_get(TickType_t wait)`;`void bsp_camera_fb_return(camera_fb_t *fb);`
  - `frame_hub.h`:

```c
#define FH_SUB_DISPLAY  (1U<<0)
#define FH_SUB_EDGE_AI  (1U<<1)
#define FH_SUB_NET      (1U<<2)
typedef struct {
    uint8_t       *buf;      /* RGB565 小端 240x240 */
    uint32_t       frame_id; /* 递增 */
    TickType_t     timestamp;
} frame_t;
typedef void (*frame_cb_t)(const frame_t *f);
void frame_hub_init(void);
void frame_hub_set_mask(uint32_t mask);           /* 模式切换核心 */
uint32_t frame_hub_get_mask(void);
void frame_hub_dispatch(const frame_t *f);        /* 相机任务调用,同步顺序回调 */
void frame_hub_on_frame(uint32_t sub, frame_cb_t cb); /* 注册订阅者回调 */
```
  - `bsp_key.h`:`void bsp_key_init(void);` 事件回调注册 `void bsp_key_set_cb(void (*on_key)(int key, int event));`(key: 1=K1/GPIO14, 2=K2/GPIO15;event: 0=短按, 1=长按≥800ms),内部上拉 + 20ms 消抖 + 长按检测,GPIO ISR→队列→任务。

**核心实现要点:**

- 相机配置(esp32-camera v2 API):

```c
camera_config_t cfg = {
    .pin_pwdn  = -1,  /* 硬件固定接地 */
    .pin_reset = -1,  /* 硬件经10k上拉 */
    .pin_xclk  = -1,  /* ATK-MC2640 自带 24MHz 晶振,载板未连 XCLK */
    .pin_sccb_sda = PIN_CAM_SDA, /* GPIO4 */
    .pin_sccb_scl = PIN_CAM_SCL, /* GPIO5 */
    .pin_d7..d0 = 16,17,18,12,10,8,9,11,      /* 按宏 */
    .pin_vsync = PIN_CAM_VSYNC, .pin_href = PIN_CAM_HREF, .pin_pclk = PIN_CAM_PCLK,
    .xclk_freq_hz = 0,          /* 外部晶振 */
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .pixel_format = PIXFORMAT_RGB565,
    .frame_size = FRAMESIZE_240X240,
    .grab_mode = CAMERA_GRAB_LATEST,
};
esp_camera_init(&cfg);
/* PCLK 无 XCLK 分频约束时可尝试 sensor 端:
   sensor_t *s = esp_camera_sensor_get();
   s->set_framesize(s, FRAMESIZE_240X240); s->set_vflip(s, 1); 按模组安装方向定 */
```
若所用 esp32-camera 版本不允许 `xclk_freq_hz=0`,改为 `24000000`(传感器时钟实际来自晶振,该字段仅影响驱动内部计算)。
- 相机任务(优先级 5,栈 6KB):循环 `fb_get`→组装 `frame_t`→`frame_hub_dispatch`→`fb_return`;帧间 `vTaskDelay(1)` 让出。
- K1/K2:内部上拉,下降沿 + 20ms 消抖确认;长按 800ms 重复上报一次。

**Steps:**

- [ ] **Step 4.1** 实现 `frame_hub.c`(回调数组 + 原子掩码,dispatch 直接同步调用,注释说明回调运行在相机任务上下文)。
- [ ] **Step 4.2** 实现 `bsp_camera.c`(上述 cfg;init 失败返回负错误码)。
- [ ] **Step 4.3** 实现 `bsp_key.c`(ISR→`xQueueSendFromISR`→任务消抖/长按→回调)。
- [ ] **Step 4.4** `main.c` 临时自检:初始化相机后抓 1 帧打印 `len==240*240*2`,K1 按下打印事件。
- [ ] **Step 4.5** 验证:`idf.py build`。
- [ ] **Step 4.6** Commit:`feat(fw): OV2640(xclk=-1)+帧分发器+K1/K2按键`

### Task 5: 边缘 AI(TFLM)

**Files:**
- Modify: `firmware/main/app_edge_ai/edge_ai.c`
- Create: `firmware/main/app_edge_ai/edge_ai.h`

**Interfaces:**
- Consumes: `frame_t`(frame_hub)、FATFS `/storage`。
- Produces:
  - `typedef struct { int class_id; float confidence; int inference_ms; } edge_result_t;`
  - `int edge_ai_load(void);`(挂载 FATFS→读 `/storage/model_int8.tflite` 至 PSRAM→读 labels;成功 0)
  - `bool edge_ai_ready(void);`
  - `int edge_ai_classify(const frame_t *f, edge_result_t *out);`(预处理+Invoke+softmax)
  - `int edge_ai_class_count(void);` / `const char *edge_ai_label(int i);`

**核心实现要点:**

- 挂载:在 `main.c` 里 `esp_vfs_fat_spiflash_mount_rw_wl("/storage", "storage", &cfg, &wl_handle)`,分区表 SubType `fat`。
- TFLM(esp-tflite-micro 提供 `tensorflow/lite/micro` 头):

```c
/* arena 200KB PSRAM */
static uint8_t *s_arena = heap_caps_malloc(200*1024, MALLOC_CAP_SPIRAM);
tflite::MicroInterpreter interpreter(model, resolver, s_arena, 200*1024);
TfLiteTensor *input  = interpreter.input(0);   /* int8 [1,96,96,3], scale/zero_point 从张量读 */
TfLiteTensor *output = interpreter.output(0);  /* int8 logits */
```
- 预处理(相机帧 240×240 RGB565 小端 → 96×96):最近邻 2.5:1 采样(源步进 240/96=2.5,用定点累加器避免浮点),`rgb565 >> 11` 取 R5、`>>5 & 0x3F` G6、`& 0x1F` B5;int8 量化 `v = (px_norm * input->params.scale + input->params.zero_point)`(px_norm = c/31.0f 或 c/63.0f,再按模型训练侧约定缩放到 [-1,1]:`px_norm*2-1`,与训练脚本一致——训练脚本用 `(x/127.5-1)` 对应 uint8 0..255,固件端等价于 `raw_5bit*255/31 → /127.5 - 1`)。
- softmax:对 int8 logits 反量化为 float 后计算。
- 失败路径:模型缺失/校验错 → `edge_ai_ready()==false`,UI 禁用边缘模式(spec §6)。

**Steps:**

- [ ] **Step 5.1** 实现 `edge_ai.c` 全部接口(要点如上)。
- [ ] **Step 5.2** `main.c`:启动时 `edge_ai_load()`,打印模型输入尺寸/类别数;临时对一帧调 `edge_ai_classify` 打印结果(无模型时打印明确错误)。
- [ ] **Step 5.3** 验证:`idf.py build`。
- [ ] **Step 5.4** Commit:`feat(fw): TFLM边缘推理(FATFS加载/int8量化/softmax)`

### Task 6: 网络(WiFi STA + TCP 协议)

**Files:**
- Modify: `firmware/main/app_net/wifi_sta.c`、`firmware/main/app_net/pc_link.c`
- Create: `firmware/main/app_net/wifi_sta.h`、`firmware/main/app_net/pc_link.h`、`firmware/main/app_net/proto.h`

**Interfaces:**
- Produces:
  - `wifi_sta.h`:`void wifi_sta_start(const char *ssid, const char *pass);`(阻塞至获 IP 或 15s 超时)、`bool wifi_sta_is_up(void);`、`void wifi_sta_get_ip(char *ip, size_t n);`、事件回调 `wifi_sta_set_cb(void (*on_up)(void), void (*on_down)(void))`。
  - `proto.h`(协议常量与帧结构,设备/PC 共用定义):

```c
#define PROTO_MAGIC        0xA55A1234UL
#define PROTO_TYPE_HELLO   0x01   /* payload: u8 proto_ver, u8 dev_name[16], u32 fw_ver */
#define PROTO_TYPE_IMAGE   0x02   /* u16 w, u16 h, u8 fmt(0=RGB565LE), u8 rsv, u32 frame_id, pixels */
#define PROTO_TYPE_DETECT  0x03   /* u32 frame_id, u16 n, n×{f32 x,y,w,h, u8 cls, u8 _pad, f32 conf} */
#define PROTO_TYPE_HEARTBEAT 0x04 /* u32 ts */
#define PROTO_TYPE_COMMAND 0x05   /* u8 cmd(0x01 开始推流 0x02 停止 0x03 单帧), u8 arg */
typedef struct __attribute__((packed)) {
    uint32_t magic; uint16_t type; uint16_t len;
} proto_hdr_t;
```
  - `pc_link.h`:`void pc_link_start(const char *host, uint16_t port);`、`bool pc_link_is_up(void);`、`int pc_link_send_image(const frame_t *f);`(组包+发送,失败返回负)、检测结果回调 `pc_link_set_on_detect(void (*cb)(const uint8_t *payload, int len));`(payload 按 DETECT 结构解析)、心跳 2s。
- TCP 客户端:独立任务(栈 6KB),断线 3s 重连;接收循环带 magic 重同步(逐字节扫描直到 `PROTO_MAGIC`)。

**Steps:**

- [ ] **Step 6.1** 实现 `wifi_sta.c`(esp_wifi STA + 事件循环 + 重连标志)。
- [ ] **Step 6.2** 实现 `pc_link.c`(连接管理/发送/接收解析/重同步/心跳)。
- [ ] **Step 6.3** 验证:`idf.py build`;真机连 WiFi 后 `pc_link` 连 PC 服务器(可用 `python -m http.server` 之外临时 `nc -l 3333` 观察 HELLO)。
- [ ] **Step 6.4** Commit:`feat(fw): WiFi STA+PC链路TCP协议(图像/检测结果/心跳)`

### Task 7: UI 框架 + 主页 + 检测页

**Files:**
- Modify: `firmware/main/ui/ui.c`、`ui_theme.c`、`ui_page_home.c`、`ui_page_detect.c`,`main.c`
- Create: `firmware/main/ui/ui.h`、`firmware/main/ui/ui_theme.h`、各页面 `.h`

**Interfaces:**
- Consumes: 前述全部模块(frame_hub、edge_ai、pc_link、app_nvs、bsp_key)。
- Produces:
  - `ui.h`:`void ui_init(void);`、`void ui_switch_page(int page_id);`(PAGE_HOME/DETECT/CAPTURE/STATS/SETTINGS)、`void ui_set_preview_frame(const frame_t *f);`(线程安全:内部 `bsp_display_lvgl_lock`)、`void ui_show_detect_result(const edge_result_t *r);`、`void ui_show_pc_boxes(const uint8_t *payload, int len);`、`void ui_refresh_status(void);`(主页状态卡刷新)、检测模式枚举 `UI_MODE_PREVIEW/EDGE/PC`。
  - 预览实现:`lv_image_dsc_t` 静态描述符(`header.w=240,h=240,stride=480,cf=LV_COLOR_FORMAT_RGB565`)+ `lv_image` widget,`data` 指向相机 fb(小端,LVGL 源按本机序读,SW_SWAP 只作用于显示缓冲,链路成立);每次 `ui_set_preview_frame` 更新 data 指针并 `lv_obj_invalidate`。
  - bbox 叠加:4~8 个复用的 `lv_obj` 边框矩形(2px 边框,NG 红 `0xFF4444`/OK 绿 `0x2ECC71`),按 DETECT payload 坐标(原图 240×240 坐标系=预览 1:1)移动/显隐;左上角类别+置信度小标签。
  - K1=单次检测,K2=返回主页(`bsp_key` 回调 → `ui` 层分派,注意从 LVGL 锁外调用 `ui_switch_page` 时加锁)。
- 检测页布局(240×320):顶部状态条(16px,模式/IP/连接点)→ 预览 240×240 → 结果区:类别大字 + 置信度 `lv_bar` + 耗时 + 帧率;模式 segmented + 单次按钮 + 连续开关放底部。

**Steps:**

- [ ] **Step 7.1** `ui_theme.c`:深色工业风样式表(背景 `0x1A1D23`,卡片 `0x242933`,主色 `0x00B4D8`,警示 `0xFF6B6B`,OK `0x2ECC71`,字体 `lv_font_simsun_16_cjk` 全局)。
- [ ] **Step 7.2** `ui.c` 页面管理(页面表 + `lv_obj` 创建/隐藏切换)+ `ui_set_preview_frame` + K1/K2 分派。
- [ ] **Step 7.3** 主页:标题"PCB 焊点检测仪"、状态卡(WiFi/IP/PC/模型/帧率,`ui_refresh_status` 轮询刷新)、四入口大按钮(检测/采集/统计/设置)。
- [ ] **Step 7.4** 检测页:布局+模式切换逻辑(`frame_hub_set_mask`:PREVIEW=DISPLAY;EDGE=DISPLAY|EDGE_AI;PC=DISPLAY|NET)+ 边缘结果 UI + PC bbox 叠加 + 单次/连续;`main.c` 里 frame_hub 各订阅者接到 ui/edge_ai/pc_link。
- [ ] **Step 7.5** 验证:`idf.py build`。
- [ ] **Step 7.6** Commit:`feat(fw): UI框架/主题/主页/检测页(预览+bbox叠加+模式切换)`

### Task 8: 采集页 + 统计页 + 设置页 + 校准 UI

**Files:**
- Modify: `firmware/main/ui/ui_page_capture.c`、`ui_page_stats.c`、`ui_page_settings.c`、`ui_calib.c`

**Interfaces (Produces,均由 ui.h 已有入口调用):**
- 采集页:预览 + 6 类 `lv_obj` 可选 chip + 保存按钮 → `/storage/captures/<label>/NNN.bmp`(BMP24 写文件,`frame_t` RGB565→BGR888)+ 剩余空间显示(`esp_vfs_fat_info`)。
- 统计页:6 类计数行 + `lv_bar` 比例 + 总数/NG 率 + 清零按钮(`lv_msgbox` 确认);数据来自 `app_nvs_get_class_count`。
- 设置页:WiFi SSID/密码(自制 `lv_keyboard` + 文本框,密码掩码)、PC `ip:port` 文本框、阈值滑条(1~99)、背光滑条 + 即时生效、触摸校准按钮(进入校准流程:十字靶左上/右下,调 `ui_calib_collect`)、恢复默认(确认弹窗→清 NVS 配置区保留统计→重启)。
- 校准 UI:全屏黑底 + 十字靶 2 点采集(等触摸按下取 raw)→ 写 NVS → 提示完成。

**Steps:**

- [ ] **Step 8.1** 采集页(BMP 写入函数 `static int save_bmp24(const char *path, const frame_t *f)`:54B 头 + 240*240*3,行序自下而上,像素 BGR)。
- [ ] **Step 8.2** 统计页 + NVS 计数联动(边缘/PC 判定 NG 时 `app_nvs_add_class_count`,注意只统计"单次检测"避免连刷)。
- [ ] **Step 8.3** 设置页全部控件 + 保存逻辑(保存即写 NVS;WiFi/PC 地址保存后提示"返回主页生效",`main.c` 里状态机重启对应连接)。
- [ ] **Step 8.4** 校准 UI 流程串起来。
- [ ] **Step 8.5** 验证:`idf.py build`。
- [ ] **Step 8.6** Commit:`feat(fw): 采集/统计/设置/校准页面,固件功能完备`

### Task 9: PC 端应用 + 协议测试

**Files:**
- Create: `pc/inspector_pc.py`、`pc/proto.py`、`pc/requirements.txt`、`pc/tests/test_proto.py`

**Interfaces:**
- `pc/proto.py`(与固件 proto.h 严格对应):

```python
import struct
MAGIC = 0xA55A1234
TYPE_HELLO, TYPE_IMAGE, TYPE_DETECT, TYPE_HEARTBEAT, TYPE_COMMAND = 1, 2, 3, 4, 5
HDR = struct.Struct('<IHH')          # magic, type, len
IMG_HEAD = struct.Struct('<HHhBI')   # w, h, fmt(0=RGB565LE), rsv, frame_id   (h 有符号对齐,实际用 '<HHBBI')
BOX = struct.Struct('<ffffBBf')      # x, y, w, h, cls, pad, conf

class ProtocolError(Exception): ...
def encode(type_: int, payload: bytes) -> bytes: ...
class Decoder:            # 流式:feed(bytes) -> list[(type, payload)]
    def __init__(self): self._buf = bytearray()
    def feed(self, data: bytes) -> list[tuple[int, bytes]]:  # magic 重同步
```
- `inspector_pc.py` 布局:左=画面(QImage RGB565→RGB888:numpy `(buf.view('<u2') >> 11 & 0x1F)*255//31` 等查 LUT)+ YOLO 框叠加;右=连接(监听 :3333,设备列表/心跳)、推理(模型路径 `yolov8n.pt` 默认、阈值滑条、开关)、采集(类别下拉+保存到 `pc/dataset/<label>/`、计数)、日志/统计。YOLOv8:`from ultralytics import YOLO; model(frame_bgr, conf=thr, verbose=False)`,结果组 BOX payload 回传;推理开关关闭时纯监视。
- 网络线程:`socketserver.ThreadingTCPServer` 或手写 `selectors`,主界面经 Qt 信号更新(PySide6 `Signal`),禁止在工作线程碰 UI。

**Steps:**

- [ ] **Step 9.1** 实现 `pc/proto.py`。
- [ ] **Step 9.2** 写 `pc/tests/test_proto.py`(pytest,无硬件可跑):

```python
from pc.proto import encode, Decoder, TYPE_IMAGE, MAGIC

def test_roundtrip():
    payload = b'\x00' * 16
    pkt = encode(TYPE_IMAGE, payload)
    d = Decoder()
    out = d.feed(b'\xFF\xFF' + pkt + b'\x99')   # 前后夹垃圾字节
    assert out == [(TYPE_IMAGE, payload)]        # 重同步+完整解出
    assert len(d._buf) == 1                      # 残留 1 字节垃圾
```

- [ ] **Step 9.3** 运行 `python -m pytest pc/tests/ -v` → PASS。
- [ ] **Step 9.4** 实现 `inspector_pc.py` 完整 GUI。
- [ ] **Step 9.5** 冒烟:`python -c "import ast;ast.parse(open('pc/inspector_pc.py',encoding='utf-8').read())"`;有依赖环境则起 GUI 手测。
- [ ] **Step 9.6** Commit:`feat(pc): PySide6检测台+协议库(含单元测试)`

### Task 10: 训练 + 烧录脚本

**Files:**
- Create: `pc/train_edge.py`、`pc/train_yolo.py`、`tools/flash_model.py`、`pc/labelmap.txt`

**Interfaces/要点:**
- `train_edge.py`:`--data pc/dataset --epochs 60 --batch 32 --out pc/out_edge`;类目录即标签(按 `labelmap.txt` 顺序);增强(翻转/旋转±10°/亮度 0.8~1.2/高斯噪声);8:2 分层划分;`MobileNetV2(alpha=0.35,input_shape=(96,96,3),weights='imagenet')`→GlobalAvgPool→Dense;训练后 int8 全量化:

```python
def representative_dataset():
    for x in rep_imgs:      # 训练集采样 200 张,与固件一致缩放到 [-1,1]
        yield [x.astype(np.float32)]
conv = tf.lite.TFLiteConverter.from_keras_model(model)
conv.optimizations = [tf.lite.Optimize.DEFAULT]
conv.representative_dataset = representative_dataset
conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
conv.inference_input_type = tf.int8; conv.inference_output_type = tf.int8
```
输出 `model_int8.tflite` + `labels.txt` + 测试集准确率/混淆矩阵打印;固件量化公式在脚本头注释中与 edge_ai.c 对齐(`(x/127.5-1)`)。
- `train_yolo.py`:`--data pc/dataset_yolo --model yolov8n.pt --epochs 100`,要求 `data.yaml`(脚本可 `--make-yaml` 自动生成:train/val 路径+6 类名);产出 `runs/detect/train/weights/best.pt`。
- `flash_model.py`:调 ESP-IDF 的 `fatfsgen.py` 把 `pc/out_edge/{model_int8.tflite,labels.txt}` 打包为 FAT 镜像并 `esptool.py write_flash 0x320000`;参数 `--port COMx --idf %IDF_PATH%`;Windows 批处理一键封装。
- `pc/labelmap.txt`:6 行 ok/excess/insufficient/bridge/misalignment/cold_joint 对应中文名(固件 EMBED 的 labels_default.txt 同内容)。

**Steps:**

- [ ] **Step 10.1** 实现 `train_edge.py`(含 README 式 `--help` 与输出统计)。
- [ ] **Step 10.2** 实现 `train_yolo.py`。
- [ ] **Step 10.3** 实现 `flash_model.py`。
- [ ] **Step 10.4** 冒烟:`python pc/train_edge.py --help`、`python pc/train_yolo.py --help`、`python tools/flash_model.py --help` 全部可用;`py_compile` 通过。
- [ ] **Step 10.5** Commit:`feat(tools): 边缘模型训练/YOLO训练/FATFS模型烧录脚本`

### Task 11: README + 端到端检查 + 收尾

**Files:**
- Modify: `README.md`(根)
- Create: `pc/DATA_GUIDE.md`(采集→LabelImg 标注(仅检测需要)→训练→烧录全流程图文步骤)

**Steps:**

- [ ] **Step 11.1** README:目录结构、环境(ESP-IDF 5.2+ / Python 3.10+)、`idf.py build flash monitor`、模型烧录、PC 端运行、快速上手(预览→边缘→PC 三模式)、故障排查(相机 init 失败/XCLK 说明、触摸不准→校准、帧率)。
- [ ] **Step 11.2** 汇总验证:固件 `idf.py build`(或声明未在本机执行);`pytest pc/tests`;全部脚本 `--help`;grep 检查无 TODO/TBD 残留。
- [ ] **Step 11.3** 按 spec §11 输出真机验收清单(留给用户在硬件上逐项勾)。
- [ ] **Step 11.4** Commit:`docs: 全流程README+数据指南+验收清单` 并 `git push`。

---

## Self-Review 记录

- **Spec 覆盖**:§1 目标/成功标准→Task 11 验收;§2 GPIO→Task 1 集中定义;§3 架构→Task 4 帧分发;§4 固件(目录/任务/内存/分区/LVGL 移植)→Task 1-8;§5 UI 五页→Task 7-8;§6 边缘 AI→Task 5+10;§7 协议→Task 6+9(proto.h/proto.py 同源);§8 PC 端→Task 9;§9 工具链→Task 10+11;§10 错误处理→各任务失败路径+Task 11 排查;§11 测试→各任务验证步+Task 11;§12 风险→已内嵌对策(xclk 备选写法、字体字集、数据量建议)。
- **占位符**:Task 1 Step 1.3 的"空实现占位"是编译脚手架,后续任务整文件替换,不属最终交付;其余步骤均含具体代码/命令/文件。
- **类型一致性**:`frame_t`/`edge_result_t`/proto 结构在 Task 4/5/6/7/9 间已核对一致;`ui_set_preview_frame(const frame_t*)` 与 frame_hub 发布类型一致。
