import machine
import time
import framebuf

# Pin Configuration (Matching our ESP32-C3 hardware SPI pins)
BUSY = machine.Pin(1, machine.Pin.IN)
RST  = machine.Pin(3, machine.Pin.OUT)
DC   = machine.Pin(2, machine.Pin.OUT)
CS   = machine.Pin(10, machine.Pin.OUT)

# Initialize Hardware SPI on the ESP32-C3
spi = machine.SPI(1, baudrate=4000000, polarity=0, phase=0, sck=machine.Pin(4), mosi=machine.Pin(6))

# Define Display Dimensions
WIDTH = 200
HEIGHT = 200

# Allocate memory buffer for a 200x200 monochrome display (1 bit per pixel)
buffer = bytearray(WIDTH * HEIGHT // 8)
fb = framebuf.FrameBuffer(buffer, WIDTH, HEIGHT, framebuf.MONO_HLSB)

def send_command(cmd):
    DC.value(0)
    CS.value(0)
    spi.write(bytes([cmd]))
    CS.value(1)

def send_data(data):
    DC.value(1)
    CS.value(0)
    spi.write(bytes([data]))
    CS.value(1)

def wait_until_idle():
    while BUSY.value() == 1: # 1 means busy
        time.sleep_ms(10)

def init_display():
    # Hard reset the panel
    RST.value(0)
    time.sleep_ms(100)
    RST.value(1)
    time.sleep_ms(100)
    
    # Simple initialization sequence for SSD1681 driver
    send_command(0x12) # SW Reset
    wait_until_idle()

def update_display():
    send_command(0x24) # Write RAM
    for byte in buffer:
        send_data(byte)
        
    send_command(0x22) # Display Update Control 2
    send_data(0xF7)
    send_command(0x20) # Master Activation
    wait_until_idle()

# --- Main Program Execution ---
print("Initializing display...")
init_display()

# Draw on our virtual layout buffer
fb.fill(1) # Clear screen to white (1 is usually white on ePapers)

# Draw shapes and text
fb.rect(5, 5, 190, 190, 0) # Draw black border
fb.text("MicroPython!", 20, 40, 0)
fb.text("ESP32-C3 Mini", 20, 70, 0)
fb.text("Live Coding!", 20, 100, 0)

print("Pushing buffer to ePaper screen...")
update_display()

print("Done! Going to sleep mode.")
send_command(0x10) # Deep sleep command for ePaper
send_data(0x01)