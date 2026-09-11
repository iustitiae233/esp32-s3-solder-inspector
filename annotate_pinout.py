from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
SCALE = 2
image = Image.open(ROOT / "引脚图（空）.PNG").convert("RGB")
image = image.resize((3200, 3200), Image.Resampling.LANCZOS)
draw = ImageDraw.Draw(image)
COLORS = {
    "cam": "#087E8B",
    "lcd": "#245BBD",
    "touch": "#8B3DB0",
    "aux": "#A95C08",
    "power": "#BC3040",
    "gnd": "#444C54",
    "reserved": "#808791",
}


def text(x, y, value, size=18, color="#26313B", bold=False, align="left"):
    font = ImageFont.truetype(
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        size * SCALE,
    )
    width = draw.textlength(value, font=font) / SCALE
    if align == "right":
        x -= width
    draw.text((x * SCALE, y * SCALE), value, font=font, fill=color)
    return width


def line(points, color, width=1):
    draw.line([(int(x * SCALE), int(y * SCALE)) for x, y in points],
              fill=color, width=width * SCALE)


left = [
    ("3V3", "J2-1 / J3 电源 / 上拉 / TP", "power"),
    ("3V3", "同一 3V3 电源网络", "power"),
    ("RST", "复位网络 → 可选 TP", "reserved"),
    ("GPIO4", "CAM_SDA / SIOD → J3", "cam"),
    ("GPIO5", "CAM_SCL / SIOC → J3", "cam"),
    ("GPIO6", "CAM_VSYNC → J3", "cam"),
    ("GPIO7", "CAM_HREF → J3", "cam"),
    ("GPIO15", "KEY_K2 → 按键 / C2 → GND", "aux"),
    ("GPIO16", "CAM_D7 / Y9 → J3", "cam"),
    ("GPIO17", "CAM_D6 / Y8 → J3", "cam"),
    ("GPIO18", "CAM_D5 / Y7 → J3", "cam"),
    ("GPIO8", "CAM_D2 / Y4 → J3", "cam"),
    ("GPIO3", "禁用 · 启动配置脚", "reserved"),
    ("GPIO46", "禁用 · 启动配置脚", "reserved"),
    ("GPIO9", "CAM_D1 / Y3 → J3", "cam"),
    ("GPIO10", "CAM_D3 / Y5 → J3", "cam"),
    ("GPIO11", "CAM_D0 / Y2 → J3", "cam"),
    ("GPIO12", "CAM_D4 / Y6 → J3", "cam"),
    ("GPIO13", "CAM_PCLK → J3", "cam"),
    ("GPIO14", "KEY_K1 → 按键 / C1 → GND", "aux"),
    ("5Vin", "无 5V 负载；载板不接", "reserved"),
    ("GND", "全板共地", "gnd"),
]
right = [
    ("GND", "全板共地", "gnd"),
    ("TX", "UART0 console 保留", "reserved"),
    ("RX", "UART0 console 保留", "reserved"),
    ("GPIO1", "TP_MISO / T_DO → J2-13", "touch"),
    ("GPIO2", "TP_CS / T_CS → J2-11", "touch"),
    ("GPIO42", "LCD_CS → J2-3", "lcd"),
    ("GPIO41", "LCD_MOSI → J2-6、J2-12", "lcd"),
    ("GPIO40", "LCD_SCLK → J2-7、J2-10", "lcd"),
    ("GPIO39", "LCD_RST → J2-4", "lcd"),
    ("GPIO38", "LCD_DC → J2-5", "lcd"),
    ("GPIO37", "禁用 · Octal PSRAM", "reserved"),
    ("GPIO36", "禁用 · Octal PSRAM", "reserved"),
    ("GPIO35", "禁用 · Octal PSRAM", "reserved"),
    ("GPIO0", "板载 BOOT / 快门；载板不接", "reserved"),
    ("GPIO45", "禁用 · 启动配置脚", "reserved"),
    ("GPIO48", "板载 RGB 保留；载板不接", "reserved"),
    ("GPIO47", "TP_IRQ / T_IRQ → J2-14", "touch"),
    ("GPIO21", "LCD_BL → J2-8", "lcd"),
    ("GPIO20", "保留 · USB OTG；载板不接", "reserved"),
    ("GPIO19", "保留 · USB OTG；载板不接", "reserved"),
    ("GND", "全板共地", "gnd"),
    ("GND", "全板共地", "gnd"),
]

text(60, 45, "焊点检测仪 · 核心板引脚对应图", 36, bold=True)
text(62, 103, "YD-ESP32-S3  |  按 PCB 设计方案 v1.2 标注  |  2026-09-11", 19)
text(62, 139, "元件面正视：天线朝上、USB 朝下。左右按照片，不可直接当作焊接面或封装针号。", 19)

x = 64
for key, label in [("cam", "J3 相机"), ("lcd", "J2 显示 / 共用 SPI"),
                   ("touch", "J2 触摸"), ("aux", "按键"),
                   ("power", "电源"), ("reserved", "禁用 / 保留")]:
    line([(x, 201), (x + 22, 201)], COLORS[key], 5)
    x += 32 + text(x + 32, 187, label, 18, COLORS[key]) + 30

text(548, 271, "引脚 / 丝印", 17, bold=True, align="right")
text(430, 271, "载板信号 → 去向", 17, bold=True, align="right")
text(1062, 271, "引脚 / 丝印", 17, bold=True)
text(1180, 271, "载板信号 → 去向", 17, bold=True)

ys = [336, 375, 415, 454, 494, 534, 574, 614, 654, 694, 733,
      773, 813, 853, 893, 932, 972, 1012, 1052, 1092, 1132, 1172]
assert len(left) == len(right) == len(ys) == 22
active = [pin for pin, _, category in left + right
          if category in {"cam", "lcd", "touch", "aux"}]
assert len(active) == len(set(active)) == 24

for side, rows in [("left", left), ("right", right)]:
    for y, (pin, label, category) in zip(ys, rows):
        color = COLORS[category]
        if side == "left":
            pin_width = text(548, y - 15, pin, 18, color, True, align="right")
            width = text(430, y - 15, label, 17, color, align="right")
            assert 430 - width > 65, label
            assert 548 - pin_width > 450, pin
            line([(562, y), (600, y)], color, 2)
            line([(65, y + 22), (548, y + 22)], "#E9EDF0")
            cx = 607
        else:
            line([(1010, y), (1048, y)], color, 2)
            text(1062, y - 15, pin, 18, color, True)
            width = text(1180, y - 15, label, 17, color)
            assert 1180 + width < 1555, label
            line([(1062, y + 22), (1545, y + 22)], "#E9EDF0")
            cx = 1003
        draw.ellipse(((cx - 5) * SCALE, (y - 5) * SCALE,
                      (cx + 5) * SCALE, (y + 5) * SCALE),
                     outline=color, width=2 * SCALE)

line([(62, 1244), (1540, 1244)], "#BCC7CE", 2)
text(62, 1263, "接口与施工备注", 23, bold=True)
notes = [
    "J2 屏幕：1=3V3，2=GND，9=SDO 悬空；6/12 共 MOSI，7/10 共 SCLK；11 脚经 10kΩ 上拉至 3V3。",
    "J3 相机：ATK-MC2640 使用 2×9 排母；模块自带 24MHz 晶振，不接 XCLK；针序按原厂 P1 定义。",
    "相机控制：PWDN 接 GND，RST 经 10kΩ 上拉至 3V3，FLASH 经 10kΩ 下拉至 GND；SDA/SCL 各经 4.7kΩ 上拉。",
    "K1/K2：GPIO14、GPIO15 启用内部上拉；C1/C2 各 100nF，分别接对应按键信号与 GND。",
    "供电：USB 给核心板供电，核心板 LDO 输出 3.3V 至屏幕和相机；5Vin 无负载，载板不接。",
    "照明：载板不含灯环或灯光接口；GPIO48 仅保留核心板板载 RGB。当前共使用 24 个 GPIO。",
]
for i, note in enumerate(notes):
    width = text(62, 1305 + i * 36, note, 18,
                 "#8E4E16" if i >= 4 else "#42515E")
    assert 62 + width < 1545, note
text(62, 1546, "依据：2026-09-06-solder-ai-carrier-pcb.md v1.2；物理位置依据核心板资料与原始照片丝印。",
     16, "#71808A")

output = ROOT / "引脚图（已完善）.PNG"
image.save(output, dpi=(200, 200))
print(f"Saved: {output}")
print(f"Verified: 44 physical pins, {len(active)} unique assigned GPIOs; label bounds passed.")
