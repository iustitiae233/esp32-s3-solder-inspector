try:
  import usocket as socket
except:
  import socket
from machine import Pin
import network
led = Pin(2, Pin.OUT)
led.value(1)
# 用来绑定端口
selfadd = '0'
# 该函数是手机或者电脑连esp32板子的wifi
def do_ap_connet():
  # 创建站点接口
  ap = network.WLAN(network.AP_IF) 
  # 设置esp32板子的wifi名字
  ap.config(essid='ESP-AP') #  配置实例的essid参数
  # 限制客户端连接它的数量 
  ap.config(max_clients=2) 

  # 检查是否有设备连接ap
  if not ap.isconnected(): 
    print('connecting to network...')
    # 开启无线热点
    ap.active(True)         
    while not ap.isconnected():
      pass 
  print('network config:', ap.ifconfig())
  # 记录ip地址
  selfadd=ap.ifconfig()[0]
  led.value(0)

 
# 手机或者其他设备连接esp32开启的热点
do_ap_connet()
