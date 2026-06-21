import machine
import time
from ssd1681 import SSD1681

# 1. Initialize Safe Software SPI (Cleans up deprecation warning)
spi = machine.SoftSPI(
    baudrate=2000000, 
    polarity=0, 
    phase=0, 
    sck=machine.Pin(4), 
    mosi=machine.Pin(5), 
    miso=machine.Pin(0)
)

# 2. Control Pins
display = SSD1681(
    spi=spi,
    cs=machine.Pin(7, machine.Pin.OUT),
    dc=machine.Pin(6, machine.Pin.OUT),
    rst=machine.Pin(3, machine.Pin.OUT),
    busy=machine.Pin(2, machine.Pin.IN)
)

print("Clearing canvas...")
display.framebuf.fill(1) # Clear to White

# 3. Draw Shapes / Text using the internal framebuffer toolset
display.framebuf.rect(5, 5, 190, 190, 0)
display.framebuf.text("Hello World!", 45, 80, 0)
display.framebuf.text("Driver Active", 45, 110, 0)

print("Refreshing panel...")
display.show()
print("Done!")