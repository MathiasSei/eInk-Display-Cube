# font_renderer.py - Lightweight custom font driver for MicroPython framebuf
import framebuf

class FontRenderer:
    def __init__(self, fbuf, font_module):
        self.fbuf = fbuf
        self.font = font_module

    def draw_text(self, text, x, y, color=0):
        current_x = x
        for char in text:
            # Fetch character metadata from the font file
            glyph, width, height = self.font.get_ch(char)
            if glyph:
                # Create a temporary frame representation for the character
                char_fb = framebuf.FrameBuffer(bytearray(glyph), width, height, framebuf.MONO_HLSB)
                # Blit (copy) the character directly onto the main ePaper canvas
                self.fbuf.blit(char_fb, current_x, y, 1 - color) # Handles inverse mapping safely
                current_x += width # Shift x automatically for the next letter