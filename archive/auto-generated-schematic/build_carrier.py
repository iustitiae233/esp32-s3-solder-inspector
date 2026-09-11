"""Generate an EasyEDA Standard schematic and a matching review PDF."""
import csv
import json
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

pdfmetrics.registerFont(TTFont("ChineseFont", r"C:\Windows\Fonts\simhei.ttf"))

OUT = Path(__file__).parent / "carrier-18pin"
OUT.mkdir(exist_ok=True)
W, H = 1800, 1300
pdf = canvas.Canvas(str(OUT / "carrier-review.pdf"), pagesize=(W, H))
shapes, connections = [], []
serial = 0


def uid():
    global serial
    serial += 1
    return f"gge{serial}"


def text(x, y, value, size=10, mark="L"):
    pdf.setFillColorRGB(.05, .12, .2)
    pdf.setFont("ChineseFont", size)
    pdf.drawString(x, H-y, value)
    return f"T~{mark}~{x}~{y}~0~#132D40~Arial~{size}pt~~~~comment~{value}~1~start~{uid()}"


def line(x1, y1, x2, y2, wire=False):
    pdf.setStrokeColorRGB(0, .40, .24) if wire else pdf.setStrokeColorRGB(.55, .1, .1)
    pdf.setLineWidth(1)
    pdf.line(x1, H-y1, x2, H-y2)
    return f"{'W' if wire else 'PL'}~{x1} {y1} {x2} {y2}~{'#00663D' if wire else '#880000'}~1~0~none~{uid()}"


def rect(x, y, w, h):
    pdf.setStrokeColorRGB(.55, .1, .1)
    pdf.rect(x, H-y-h, w, h)
    return f"R~{x}~{y}~~~{w}~{h}~#880000~1~0~none~{uid()}"


def pin(x, y, number, name, direction=1, show=True):
    end = x + direction*20
    pdf.setStrokeColorRGB(.55, .1, .1)
    pdf.line(x, H-y, end, H-y)
    if show:
        pdf.setFont("Helvetica", 9)
        pdf.drawString(end+4 if direction == 1 else end-15, H-y+4, str(number))
        pdf.drawString(end+25 if direction == 1 else end-50, H-y-3, name)
    anchor = "start" if direction == 1 else "end"
    return (f"P~show~0~{number}~{x}~{y}~{180 if direction == 1 else 0}~{uid()}"
            f"^^{x}~{y}^^M {x} {y} h {direction*20}~#880000"
            f"^^{int(show)}~{end+direction*25}~{y+3}~0~{name}~{anchor}~~9pt"
            f"^^{int(show)}~{end+direction*4}~{y-4}~0~{number}~{anchor}~~8pt"
            f"^^0~{end}~{y}^^0~M {end} {y}")


def net(x, y, name, direction=-1):
    if name is None:
        shapes.append(f"O~{x}~{y}~{uid()}~M{x-4},{y-4} L{x+4},{y+4} M{x+4},{y-4} L{x-4},{y+4}~#FF0000")
        pdf.line(x-4, H-y+4, x+4, H-y-4)
        pdf.line(x+4, H-y+4, x-4, H-y-4)
        return
    ex = x + direction*35
    shapes.append(line(x, y, ex, y, True))
    anchor = "end" if direction < 0 else "start"
    shapes.append(f"N~{ex}~{y}~0~#00663D~{name}~{uid()}~{anchor}~{ex}~{y-3}~Arial~9pt")
    pdf.setFillColorRGB(0, .35, .2)
    pdf.setFont("Helvetica", 9)
    (pdf.drawRightString if direction < 0 else pdf.drawString)(ex, H-y+3, name)


def symbol(ref, value, x, y, parts):
    attrs = f"package``nameAlias`Value`Value`{value}`name`{value}`pre`{ref}`"
    shapes.append(f"LIB~{x}~{y}~{attrs}~0~0~{uid()}#@$" +
                  "#@$".join([text(x, y-30, ref, 11, "P"), text(x, y-15, value, 10, "N")] + parts))


def connector(ref, value, x, y, pins):
    parts = [rect(x, y, 155, len(pins)*22+12)]
    for i, (label, network) in enumerate(pins, 1):
        py = y+i*22
        parts.append(pin(x-20, py, i, label))
        net(x-20, py, network)
        connections.append((ref, i, label, network or "NC"))
    symbol(ref, value, x, y, parts)


def one_pin_connector(ref, x, y, label, network):
    """Represent one development-board socket position as a 1x1 female header."""
    # Keep the dense J1 area readable: the reference identifies the individual
    # socket; its repeated generic 1x1P value is intentionally hidden.
    parts = [rect(x, y, 110, 18), pin(x - 20, y + 9, 1, label),
             text(x + 5, y + 13, ref, 6)]
    net(x - 20, y + 9, network)
    connections.append((ref, 1, label, network or "NC"))
    attrs = f"package``nameAlias`Value`Value`FEMALE HEADER 1x1 2.54`pre`{ref}`"
    shapes.append(f"LIB~{x}~{y}~{attrs}~0~0~{uid()}#@$" + "#@$".join(parts))


def passive(ref, value, x, y, a, b, kind="R"):
    parts = [pin(x-40, y, 1, "A" if kind == "D" else "1", show=False),
             pin(x+40, y, 2, "K" if kind == "D" else "2", -1, False)]
    if kind == "C":
        parts += [line(x-20, y, x-4, y), line(x+4, y, x+20, y),
                  line(x-4, y-10, x-4, y+10), line(x+4, y-10, x+4, y+10)]
    elif kind == "D":
        parts += [line(x-20, y, x-10, y), line(x+10, y, x+20, y),
                  line(x-10, y-10, x-10, y+10), line(x-10, y-10, x+10, y),
                  line(x-10, y+10, x+10, y), line(x+10, y-10, x+10, y+10)]
    elif kind == "SW":
        parts += [line(x-20, y, x-10, y), line(x-10, y, x+10, y-12),
                  line(x+10, y, x+20, y)]
    else:
        parts += [rect(x-20, y-6, 40, 12)]
    symbol(ref, value, x-20, y, parts)
    net(x-40, y, a)
    net(x+40, y, b, 1)
    connections.extend([(ref, 1, "A" if kind == "D" else "1", a),
                        (ref, 2, "K" if kind == "D" else "2", b)])


shapes.append(text(55, 45, "焊点检测仪载板 / YD-ESP32-S3 + ATK-OV2640 V2.2", 22))
shapes.append(text(55, 70, "版本 0.3 草稿 | 2026-09-09 | 嘉立创 EDA 标准版 | 全部直插 | 已删除外接光源", 12))
left_names = ["3V3","3V3","RST","IO4","IO5","IO6","IO7","IO15","IO16","IO17","IO18",
              "IO8","IO3","IO46","IO9","IO10","IO11","IO12","IO13","IO14","5Vin","GND"]
left_nets = ["3V3","3V3","RST","CAM_SDA","CAM_SCL","CAM_VSYNC","CAM_HREF",None,
             "CAM_D7","CAM_D6","CAM_D5","CAM_D2",None,None,"CAM_D1","CAM_D3",
             "CAM_D0","CAM_D4","CAM_PCLK","KEY_K1","5V","GND"]
right_names = ["GND","TX","RX","IO1","IO2","IO42","IO41","IO40","IO39","IO38","IO37",
               "IO36","IO35","IO0","IO45","IO48","IO47","IO21","IO20","IO19","GND","GND"]
right_nets = ["GND",None,None,"TP_MISO","TP_CS","LCD_CS","LCD_MOSI","LCD_SCLK",
              "LCD_RST","LCD_DC",None,None,None,None,None,None,"TP_IRQ","LCD_BL",
              None,None,"GND","GND"]
for index, (label, network) in enumerate(zip(left_names, left_nets), 1):
    one_pin_connector(f"J1L{index:02d}", 220, 145 + (index - 1) * 22, label, network)
for index, (label, network) in enumerate(zip(right_names, right_nets), 1):
    one_pin_connector(f"J1R{index:02d}", 620, 145 + (index - 1) * 22, label, network)
screen_names = ["VCC","GND","CS","RESET","DC-RS","SDI","SCK","LED","SDO","T_CLK","T_CS","T_DIN","T_DO","T_IRQ"]
screen_nets = ["3V3","GND","LCD_CS","LCD_RST","LCD_DC","LCD_MOSI","LCD_SCLK","LCD_BL",
               None,"LCD_SCLK","TP_CS","LCD_MOSI","TP_MISO","TP_IRQ"]
connector("J2", "MSP2807显示屏/1x14排母", 1050, 145, list(zip(screen_names, screen_nets)))
cam_names = ["GND","3V3","SCL","VSYNC","SDA","HREF","D0","RST","D2","D1","D4","D3",
             "D6","D5","PCLK","D7","PWDN","FLASH"]
cam_nets = ["GND","3V3","CAM_SCL","CAM_VSYNC","CAM_SDA","CAM_HREF","CAM_D0","CAM_RST",
            "CAM_D2","CAM_D1","CAM_D4","CAM_D3","CAM_D6","CAM_D5","CAM_PCLK",
            "CAM_D7","GND","CAM_FLASH"]
connector("J3", "ATK-MC2640相机/2x9排母", 1500, 145, list(zip(cam_names, cam_nets)))
shapes.append(text(55, 700, "上拉电阻 / 相机固定电平 / 按键", 14))
passive("R1", "10k THT", 210, 765, "3V3", "TP_CS")
passive("R2", "10k THT", 600, 765, "3V3", "CAM_RST")
passive("R3", "10k THT", 1020, 765, "CAM_FLASH", "GND")
passive("R4", "4.7k THT", 1480, 765, "3V3", "CAM_SDA")
passive("R5", "4.7k THT", 1480, 845, "3V3", "CAM_SCL")
passive("K1", "NO PUSHBUTTON THT", 210, 845, "KEY_K1", "GND", "SW")
passive("C1", "100nF THT", 600, 845, "KEY_K1", "GND", "C")
shapes.append(text(55, 915, "就近去耦 / 测试点", 14))
for ref, x, rail in [("C2",210,"3V3"),("C3",600,"3V3"),("C4",1020,"3V3"),("C5",1480,"3V3")]:
    passive(ref, "100nF THT", x, 965, rail, "GND", "C")
for i, (x, rail) in enumerate([(220,"5V"),(620,"3V3"),(1050,"GND"),(1500,"RST")], 1):
    connector(f"TP{i}", "测试点", x, 1040, [(rail,rail)])
notes = [
    "J1由44个1x1P原理图符号组成：J1L01-J1L22、J1R01-J1R22；从天线端向USB端依次编号。",
    "相机：J3针号按原厂P1定义，2x9排针；模块自带24MHz晶振，不接载板XCLK；GPIO15未使用；共使用23个GPIO。",
    "相机：RST通过10k电阻上拉，PWDN接地，FLASH通过10k电阻下拉；确认模块R2/R3状态并关闭传感器闪光控制。",
    "显示屏：按照MSP2807原理图，模块自身J1稳压旁路跳线应短接；背光由模块上的S8050驱动。",
    "照明：本载板不包含外接光源接口和光源供电电路，请使用独立的市电、USB或直流光源。",
    "去耦位置：C2贴近J2的3V3/GND；C3贴近J3的3V3/GND；C4贴近J1的3V3/GND；C5贴近3V3上拉电阻。",
    "出板前：为每个J1的1x1P封装分配2.54mm网格；实体排母可用1x40P裁成22P；确认两排间距和供电电流。"
]
for i, note in enumerate(notes):
    shapes.append(text(55, 1140+i*23, note, 10))

doc = {"head":{"docType":"1","editorVersion":"6.5.51","newgId":True,
               "c_para":{"Prefix Start":"1"},"c_spiceCmd":None},
       "canvas":f"CA~{W}~{H}~#FFFFFF~yes~#CCCCCC~10~{W}~{H}~line~10~pixel~5~0~0",
       "shape":shapes,"BBox":{"x":0,"y":0,"width":W,"height":H},"colors":{}}
(OUT / "carrier-easyeda.json").write_text(json.dumps(doc, indent=2), encoding="utf8")
with (OUT / "connections.csv").open("w", newline="", encoding="utf-8-sig") as f:
    writer = csv.writer(f)
    writer.writerow(["Reference","Pin","Signal","Net"])
    writer.writerows(connections)
assert len(left_names) == len(right_names) == 22
assert len(cam_nets) == 18
assert cam_nets[16] == "GND" and left_nets[7] is None
assert screen_nets[5] == screen_nets[11]
assert screen_nets[6] == screen_nets[9]
assert len([n for n in left_nets+right_nets if n and n not in {"3V3","5V","GND","RST"}]) == 23
pdf.showPage()
pdf.save()
print(f"Generated {len(shapes)} shapes, {len(connections)} pin records in {OUT}")
