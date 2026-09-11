import machine
import time

pin12 = machine.Pin(12, machine.Pin.OUT)
pin12.value(0)
pin13 = machine.Pin(13, machine.Pin.IN, machine.Pin.PULL_UP)
print(pin13.value())

while True:
    print(pin13.value())
    pin12.value(0)
    time.sleep_ms(500) 
 
    pin12.value(1)
    time.sleep_ms(500) 
    
    print("End of Loop")
    
