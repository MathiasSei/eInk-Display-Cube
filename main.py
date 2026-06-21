import machine
import time
import framebuf

# 1. Turn ON the WeAct On-Board Power Switch (CRITICAL FIX)
power_pin = machine.Pin(8, machine.Pin.OUT)
power_pin.value(1) # Must be HIGH to feed power to the ePaper panel
time.sleep_ms(100) # Give the power rail a brief moment to stabilize

# 2. Configure SPI & Control Pins (Matching your ESP32-C3 setup)
BUSY = machine.Pin(1, machine.Pin.IN)
RST  = machine.Pin(3, machine.Pin.OUT)
DC   = machine.Pin(2, machine.Pin.OUT)
CS   = machine.Pin(10, machine.Pin.OUT)

spi = machine.SPI(1, baudrate=4000000, polarity=0, phase=0, sck=machine.Pin(4), mosi=machine.Pin(6))

# 3. Allocating the 200x200 canvas
# Using MONO_HLSB for standard layout tracking
buffer = bytearray(200 * 200 // 8)
fb = framebuf.FrameBuffer(buffer, 200, 200, framebuf.MONO_HLSB)

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
    while BUSY.value() == 1:
        time.sleep_ms(10)

def init_display():
    # Physical hardware line pulse sequence
    RST.value(0)
    time.sleep_ms(100)
    RST.value(1)
    time.sleep_ms(100)
    
    send_command(0x12) # Software Reset
    wait_until_idle()
    
    # Official WeAct Native Driver Settings for SSD1681
    send_command(0x01) 
    send_data(0xC7)    # Panel Height (199)
    send_data(0x00)
    send_data(0x00)
    
    send_command(0x11) # Data Entry Mode Layout Configuration
    send_data(0x03)    # X inc, Y inc
    
    send_command(0x44) # X RAM boundaries
    send_data(0x00)
    send_data(0x14)    # 20 bytes horizontally (160 px wide map limit)
    
    send_command(0x45) # Y RAM boundaries
    send_data(0x00)
    send_data(0x00)
    send_data(0xC7)
    send_data(0x00)
    
    send_command(0x3C) # Border Waveform
    send_data(0x01)
    
    send_command(0x18) # Activate temperature tracking 
    send_data(0x80)
    
    send_command(0x4E) # Set pointers to zero coordinates
    send_data(0x00)
    send_command(0x4F)
    send_data(0x00)
    send_data(0x00)
    wait_until_idle()

# --- Main Drawing Loop ---
print("Waking display engine with GPIO 8...")
init_display()

# Clear buffer to White
fb.fill(1)

# Draw Hello World text
fb.text("Hello World!", 40, 70, 0)
fb.text("WeAct Studio", 40, 110, 0)

print("Writing clean bytes to layout RAM...")
send_command(0x24) # Write RAM command
spi.write(buffer)

print("Triggering panel paint refresh...")
send_command(0x22) # Display Update Control 2
send_data(0xF7)
send_command(0x20) # Master Activation
wait_until_idle()

print("Safe deep sleep...")
send_command(0x10) # Deep sleep entry register
send_data(0x01)