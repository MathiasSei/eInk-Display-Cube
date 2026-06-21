import machine
import time
from ssd1681 import SSD1681
from font_renderer import FontRenderer
import font_arial24  # Import our big 24px font data file

# 1. Start Software SPI
spi = machine.SoftSPI(
    baudrate=2000000, 
    polarity=0, 
    phase=0, 
    sck=machine.Pin(4), 
    mosi=machine.Pin(5), 
    miso=machine.Pin(0)
)

# 2. Setup display object
display = SSD1681(
    spi=spi,
    cs=machine.Pin(7, machine.Pin.OUT),
    dc=machine.Pin(6, machine.Pin.OUT),
    rst=machine.Pin(3, machine.Pin.OUT),
    busy=machine.Pin(2, machine.Pin.IN)
)

# 3. Attach our layout renderer to the display frame buffer
renderer = FontRenderer(display.framebuf, font_arial24)

print("Clearing canvas...")
display.framebuf.fill(1) # White baseline

# Draw thin border line
display.framebuf.rect(5, 5, 190, 190, 0)

# 4. Write beautiful large text! (Must use characters found in the index string)
renderer.draw_text("HELLO", 50, 40, color=0)
renderer.draw_text("ESP32-C3", 35, 85, color=0)
renderer.draw_text("WEACT 1.54", 25, 130, color=0)

print("Pushing crisp vectors to ePaper panel...")
display.show()
print("Done!")