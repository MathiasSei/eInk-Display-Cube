# ssd1681.py - MicroPython Driver for WeAct 1.54" SSD1681 ePaper
import time
from micropython import const
import framebuf

class SSD1681:
    def __init__(self, spi, cs, dc, rst, busy, width=200, height=200):
        self.spi = spi
        self.cs = cs
        self.dc = dc
        self.rst = rst
        self.busy = busy
        self.width = width
        self.height = height
        self.pages = height
        self.buffer = bytearray(self.width * self.height // 8)
        self.framebuf = framebuf.FrameBuffer(self.buffer, self.width, self.height, framebuf.MONO_HLSB)

    def _command(self, cmd, data=None):
        self.dc.value(0)
        self.cs.value(0)
        self.spi.write(bytes([cmd]))
        self.cs.value(1)
        if data is not None:
            self.dc.value(1)
            self.cs.value(0)
            self.spi.write(data)
            self.cs.value(1)

    def _wait_busy(self):
        while self.busy.value() == 1:
            time.sleep_ms(10)

    def reset(self):
        self.rst.value(0)
        time.sleep_ms(200)
        self.rst.value(1)
        time.sleep_ms(200)

    def init(self):
        self.reset()
        self._wait_busy()
        self._command(0x12) # SW Reset
        self._wait_busy()
        
        self._command(0x01, b'\xC7\x00\x00') # Driver output control
        self._command(0x11, b'\x03')         # Data entry mode (X+, Y+)
        self._command(0x44, b'\x00\x18')     # Set RAM X Address
        self._command(0x45, b'\x00\x00\xC7\x00') # Set RAM Y Address
        self._command(0x3C, b'\x05')         # Border Waveform
        self._command(0x18, b'\x80')         # Internal Temperature Sensor
        self._command(0x4E, b'\x00')         # Set RAM X counter
        self._command(0x4F, b'\x00\x00')     # Set RAM Y counter
        self._wait_busy()

    def show(self):
        self.init()
        self._command(0x24, self.buffer)
        self._command(0x22, b'\xF7')
        self._command(0x20)
        self._wait_busy()
        self._command(0x10, b'\x01') # Enter deep sleep