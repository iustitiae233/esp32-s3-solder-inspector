import time

from machine import Pin
from neopixel import NeoPixel
pin = Pin(48, Pin.OUT)   # set GPIO0 to output to drive NeoPixels
np = NeoPixel(pin,1)   # create NeoPixel driver on GPIO0 for 8 pixels
np[0] = (5,5, 5) # set the first pixel to white
np.write()              # write data to all pixels


while True:
    
    np[0] = (100,0, 0) # set the first pixel to white
    np.write()              # write data to all pixels
    time.sleep_ms(1000)
    np[0] = (0,100, 0) # set the first pixel to white
    np.write()              # write data to all pixels
    time.sleep_ms(1000)
    np[0] = (0,0, 100) # set the first pixel to white
    np.write()              # write data to all pixels
    time.sleep_ms(1000)
    np[0] = (100,100, 100) # set the first pixel to white
    np.write()              # write data to all pixels
    time.sleep_ms(1000)
    print("End of Loop")