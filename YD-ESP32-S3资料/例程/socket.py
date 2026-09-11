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

# 手机开热点的热点名称，或路由器wifi名
essid = 'TP-LINK_D68D'
# 热点的密码，或路由器wifi密码
password = '12345678'

# 该函数是连手机或者路由器的wifi
def do_wifi_connet():
  # 创建站点接口
  wlan = network.WLAN(network.STA_IF)
  # 开启网络接口
  wlan.active(True)
  # 检查是否连接到wifi
  if not wlan.isconnected():
     print('connecting to network...')
     # 连接wifi
     wlan.connect(essid, password)
     while not wlan.isconnected():
        pass
  print('network config:', wlan.ifconfig())
  # 记录ip地址
  selfadd = wlan.ifconfig()[0]
  led.value(0)
 
# esp32连接手机或者路由器的wifi 
do_wifi_connet() 

