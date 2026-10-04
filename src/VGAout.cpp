/*
 Copyright (c) 

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
*/

/*
 * TVout Graphics API for STM32F407 (320x200 Monochrome NTSC)
 *
 * Color Definitions:
 *   BLACK  = 0
 *   WHITE  = 1
 *   INVERT = 2
 */

#include <bitband.h>
#include <VGAout.h>
#define X_OFFSET  16 // Shift everything right by 16 pixels (adjust as needed)
const int pwmOutPin = PB9;      // Tone output pin placeholder

short tone_pin = -1;            // Audio output pin
short tone_freq = 444;          // Audio frequency (0 = silence)

// Initialize TTVout wrapper and pass parameters down to low-level NTSC driver
void VGAout::begin( uint8_t spino, uint8_t* extram) {//uint8_t mode,
    VGA->begin(  extram); // Start NTSC video driver & timers //mode,

    init(
        VGA->VRAM(),     // Pass base pointer of allocated frame buffer
        VGA->width(),    // 320 pixels wide
        VGA->height()    // 200 lines high
    );
}



// Memory mapping and resolution initializer
void VGAout::init(uint8_t* vram, uint16_t width, uint16_t height) {
    
    _screen = vram;  
    _width  = width;                 // 320 pixels
    _height = height;                // 200 pixels
    _hres   = _width / 8;            // 40 bytes per scanline stride
    _vres   = _height;               // 200 total active scanlines

    // Bit-banding SRAM base mapping calculation adjusted for STM32F4 bit address space
#if BITBAND == 1
    //_adr = (volatile uint32_t*)(BB_SRAM_BASE + ((uint32_t)_screen - BB_SRAM_REF) * 32);
    // SRAM bit-band reference formula for ARM Cortex-M3
    uint32_t vram_addr = (uint32_t)_screen;
    _adr = (volatile uint32_t*)(0x22000000 + ((vram_addr - 0x20000000) * 32));
#endif


}




// Frame delay - Syncs user loops directly to raster end-of-frame interrupt
void VGAout::delay_frame(uint16_t x) {
    VGA->delay_frame(x);
}

// Blanking start hook setup
void VGAout::setBktmStartHook(void (*func)()) {
    VGA->setBktmStartHook(func);
}

// Blanking end hook setup
void VGAout::setBktmEndHook(void (*func)()) {
    VGA->setBktmEndHook(func);
}

// Draw individual pixel
// Remove the #define X_OFFSET 16 completely

void VGAout::set_pixel(int16_t x, int16_t y, uint8_t d) {
    if ((x < 0) || (y < 0) || (x >= _width) || (y >= _height))
        return;
    sp(x, y, d);
}

// Clear framebuffer (Fill black)
void VGAout::cls() {
    memset(_screen, 0, _vres * _hres); // Clear 8000 bytes (320x200 / 8)
}


// Helper Sign function for line drawing
template <typename T> int _v_sgn(T val) { return (T(0) < val) - (val < T(0)); }

#ifndef abs
#define abs(a) (((a) > 0) ? (a) : -(a))
#endif

// Bresenham Line Algorithm
void VGAout::draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t dt) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = _v_sgn(x1 - x0), sy = _v_sgn(y1 - y0);
    int err = dx - dy; 

    if ((x0 != x1) || (y0 != y1)) set_pixel(x1, y1, dt);

    do { 
        set_pixel(x0, y0, dt);
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    } while ((x0 != x1) || (y0 != y1));
}

// Read color state of pixel at target coordinate
uint8_t VGAout::get_pixel(int16_t x, int16_t y) {
    if (x < 0 || x >= _width || y < 0 || y >= _height)
        return 0;

#if BITBAND == 1
    // Read bit directly from SRAM Bit-Band domain (_hres stride fixed)
    return _adr[(_hres * 8 * y) + (x & 0xf8) + 7 - (x & 7)];
#else
    // Standard bit shift read fallback
    if (_screen[(x >> 3) + y * _hres] & (0x80 >> (x & 7)))
        return 1;
    return 0;
#endif
}

// Screen-wide solid color fill
void VGAout::fill(uint8_t color) {
    _cursor_x = 0;
    _cursor_y = 0;

    switch(color) {
        case BLACK:
            memset(_screen, 0x00, _vres * _hres);
            break;
        case WHITE:
            memset(_screen, 0xFF, _vres * _hres);
            break;
        case INVERT:
            for (int32_t i = 0; i < (_vres * _hres); i++) {
                _screen[i] = ~_screen[i];
            }
            break;
    }
}

// Fast Horizontal Line Drawing
void VGAout::draw_row(int16_t line, int16_t x0, int16_t x1, uint8_t c) {
    if (line < 0 || line >= _height) return;

    uint8_t lbit, rbit;
    if (x0 == x1) {
        set_pixel(x0, line, c);
    } else {
        if (x0 > x1) {
            int16_t tmp = x0;
            x0 = x1;
            x1 = tmp;
        }

        // Clamp values within visible bounds
        if (x0 < 0) x0 = 0;
        if (x1 >= _width) x1 = _width - 1;

        lbit = 0xff >> (x0 & 7);
        x0 = x0 / 8 + _hres * line;
        rbit = ~(0xff >> (x1 & 7));
        x1 = x1 / 8 + _hres * line;

        if (x0 == x1) {
            lbit = lbit & rbit;
            rbit = 0;
        }

        if (c == WHITE) {
            _screen[x0++] |= lbit;
            while (x0 < x1) _screen[x0++] = 0xff;
            if (rbit) _screen[x0] |= rbit;
        }
        else if (c == BLACK) {
            _screen[x0++] &= ~lbit;
            while (x0 < x1) _screen[x0++] = 0x00;
            if (rbit) _screen[x0] &= ~rbit;
        }
        else if (c == INVERT) {
            _screen[x0++] ^= lbit;
            while (x0 < x1) {
                _screen[x0] = ~_screen[x0];
                x0++;
            }
            if (rbit) _screen[x0] ^= rbit;
        }
    } 
}

// Fast Vertical Line Drawing
void VGAout::draw_column(int16_t row, int16_t y0, int16_t y1, uint8_t c) {
    if (row < 0 || row >= _width) return;

    uint8_t bit;
    int16_t byte;

    if (y0 == y1) {
        set_pixel(row, y0, c);
    } else {
        if (y1 < y0) {
            int16_t tmp = y0;
            y0 = y1;
            y1 = tmp;
        }

        if (y0 < 0) y0 = 0;
        if (y1 >= _height) y1 = _height - 1;

        bit = 0x80 >> (row & 7);
        byte = row / 8 + y0 * _hres;

        if (c == WHITE) {
            while (y0 <= y1) {
                _screen[byte] |= bit;
                byte += _hres;
                y0++;
            }
        }
        else if (c == BLACK) {
            while (y0 <= y1) {
                _screen[byte] &= ~bit;
                byte += _hres;
                y0++;
            }
        }
        else if (c == INVERT) {
            while (y0 <= y1) {
                _screen[byte] ^= bit;
                byte += _hres;
                y0++;
            }
        }
    }
}

// Draw filled or outline rectangle
void VGAout::draw_rect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint8_t c, int8_t fc) {
    if (w <= 0 || h <= 0) return;
    w--;
    h--;

    if (w == 0 && h == 0) {
        set_pixel(x0, y0, c);
    } else if (w == 0 || h == 0) {
        draw_line(x0, y0, x0 + w, y0 + h, c);
    } else {
        // Draw filled center
        if (fc != -1) {
            for (int16_t i = y0; i <= y0 + h; i++) {
                draw_row(i, x0, x0 + w, fc);
            }
        }
        // Outline borders
        draw_line(x0, y0, x0 + w, y0, c);
        draw_line(x0, y0 + h, x0 + w, y0 + h, c);
        if (h > 1) { 
            draw_line(x0, y0 + 1, x0, y0 + h - 1, c);
            draw_line(x0 + w, y0 + 1, x0 + w, y0 + h - 1, c);
        }
    }
}

// Midpoint Circle Algorithm
void VGAout::draw_circle(int16_t x0, int16_t y0, int16_t radius, uint8_t c, int8_t fc) {
    int16_t f = 1 - radius;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * radius;
    int16_t x = 0;
    int16_t y = radius;
    int16_t pyy = y, pyx = x;

    if (fc != -1) 
        draw_row(y0, x0 - radius, x0 + radius, fc);

    set_pixel(x0, y0 + radius, c);
    set_pixel(x0, y0 - radius, c);
    set_pixel(x0 + radius, y0, c);
    set_pixel(x0 - radius, y0, c);

    while (x + 1 < y) {  
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        if (fc != -1) {
            if (pyy != y) {
                draw_row(y0 + y, x0 - x, x0 + x, fc);
                draw_row(y0 - y, x0 - x, x0 + x, fc);
            }
            if (pyx != x && x != y) {
                draw_row(y0 + x, x0 - y, x0 + y, fc);
                draw_row(y0 - x, x0 - y, x0 + y, fc);
            }
            pyy = y;
            pyx = x;
        }

        set_pixel(x0 + x, y0 + y, c);
        set_pixel(x0 - x, y0 + y, c);
        set_pixel(x0 + x, y0 - y, c);
        set_pixel(x0 - x, y0 - y, c);
        set_pixel(x0 + y, y0 + x, c);
        set_pixel(x0 - y, y0 + x, c);
        set_pixel(x0 + y, y0 - x, c);
        set_pixel(x0 - y, y0 - x, c);
    }
}

// Bitmap rendering with byte boundary shifts
void VGAout::bitmap(uint16_t x, uint16_t y, const unsigned char * bmp, uint16_t i, uint16_t width, uint16_t lines) {
    uint8_t temp, lshift, rshift, save, xtra;
    uint16_t si = 0;

    rshift = x & 7;
    lshift = 8 - rshift;

    if (width == 0)  width = *(bmp + i++);
    if (lines == 0) lines = *(bmp + i++);

    if (width & 7) {
        xtra = width & 7;
        width = (width / 8) + 1;
    } else {
        xtra = 8;
        width = width / 8;
    }

    for (uint8_t l = 0; l < lines; l++) {
        if ((y + l) >= _height) break; // Clamp rendering to VRAM bottom edge

        si = (y + l) * _hres + x / 8;
        temp = (width == 1) ? (0xff >> (rshift + xtra)) : 0;

        save = _screen[si];
        _screen[si] &= ((0xff << lshift) | temp);
        temp = *(bmp + i++);
        _screen[si++] |= temp >> rshift;

        for (uint16_t b = i + width - 1; i < b; i++) {
            save = _screen[si];
            _screen[si] = temp << lshift;
            temp = *(bmp + i);
            _screen[si++] |= temp >> rshift;
        }

        if ((rshift + xtra) < 8)
            _screen[si - 1] |= (save & (0xff >> (rshift + xtra)));
        if ((rshift + xtra) > 8)
            _screen[si] &= (0xff >> (rshift + xtra - 8));

        _screen[si] |= temp << lshift;
    }
}

// Fast Frame Buffer Scrolling Routine
void VGAout::shift(uint8_t distance, uint8_t direction) {
    uint8_t *src, *dst, *end;
    uint8_t shift, tmp;

    switch(direction) {
        case UP:
            dst = _screen;
            src = _screen + distance * _hres;
            end = _screen + _vres * _hres;
            while (src < end) *dst++ = *src++;
            memset(dst, 0, _screen + _vres * _hres - dst);
            break;

        case DOWN:
            dst = _screen + _vres * _hres - 1;
            src = dst - distance * _hres;
            end = _screen;
            while (src >= end) *dst-- = *src--;
            memset(_screen, 0, distance * _hres);
            break;

        case LEFT:
            shift = distance & 7;
            for (uint8_t line = 0; line < _vres; line++) {
                dst = _screen + _hres * line;
                src = dst + distance / 8;
                end = dst + _hres - 2;
                while (src <= end) {
                    tmp = *src << shift;
                    *src++ = 0;
                    tmp |= *src >> (8 - shift);
                    *dst++ = tmp;
                }
                tmp = *src << shift;
                *src = 0;
                *dst = tmp;
            }
            break;

        case RIGHT:
            shift = distance & 7;
            for (uint8_t line = 0; line < _vres; line++) {
                dst = _screen + _hres - 1 + _hres * line;
                src = dst - distance / 8;
                end = dst - _hres + 2;
                while (src >= end) {
                    tmp = *src >> shift;
                    *src-- = 0;
                    tmp |= *src << (8 - shift);
                    *dst-- = tmp;
                }
                tmp = *src >> shift;
                *src = 0;
                *dst = tmp;
            }
            break;
    }
}

// Font Selection Routine
void VGAout::select_font(const unsigned char * f) {
    _font = f;
}

// Draw Character Routine
void VGAout::print_char(uint16_t x, uint16_t y, uint8_t c) {
    c -= *(_font + 2); // Offset to starting ASCII code
    bitmap(x, y, _font, c * *(_font + 1) + 3, *_font, *(_font + 1));
}

// Advance line for terminal printing
void VGAout::inc_txtline() {
    if (_cursor_y >= (_vres - *(_font + 1)))
        shift(*(_font + 1), UP);
    else
        _cursor_y += *(_font + 1);
}

// Character and Stream Output Overloads
void VGAout::write(const char *str) { while (*str) write(*str++); }
void VGAout::write(const uint8_t *buffer, uint8_t size) { while (size--) write(*buffer++); }

void VGAout::write(uint8_t c) {
    switch(c) {
        case '\0': break;
        case '\n':
            _cursor_x = 0;
            inc_txtline();
            break;
        case 8: // Backspace
            if (_cursor_x >= *_font) _cursor_x -= *_font;
            print_char(_cursor_x, _cursor_y, ' ');
            break;
        case 13: // Carriage Return
            _cursor_x = 0;
            break;
        default:
            if (_cursor_x >= (_hres * 8 - *_font)) {
                _cursor_x = 0;
                inc_txtline();
            }
            print_char(_cursor_x, _cursor_y, c);
            _cursor_x += *_font;
            break;
    }
}

// Print Functions
void VGAout::print(const char str[]) { write(str); }
void VGAout::print(char c, int base) { print((long) c, base); }
void VGAout::print(unsigned char b, int base) { print((unsigned long) b, base); }
void VGAout::print(int n, int base) { print((long) n, base); }
void VGAout::print(unsigned int n, int base) { print((unsigned long) n, base); }

void VGAout::print(long n, int base) {
    if (base == 0) {
        write(n);
    } else if (base == 10) {
        if (n < 0) { print('-'); n = -n; }
        printNumber(n, 10);
    } else {
        printNumber(n, base);
    }
}

void VGAout::print(unsigned long n, int base) {
    if (base == 0) write(n);
    else printNumber(n, base);
}

void VGAout::print(double n, int digits) { printFloat(n, digits); }
void VGAout::println(void) { print('\r'); print('\n'); }
void VGAout::println(const char c[]) { print(c); println(); }
void VGAout::println(char c, int base) { print(c, base); println(); }
void VGAout::println(unsigned char b, int base) { print(b, base); println(); }
void VGAout::println(int n, int base) { print(n, base); println(); }
void VGAout::println(unsigned int n, int base) { print(n, base); println(); }
void VGAout::println(long n, int base) { print(n, base); println(); }
void VGAout::println(unsigned long n, int base) { print(n, base); println(); }
void VGAout::println(double n, int digits) { print(n, digits); println(); }

void VGAout::printPGM(const char str[]) {
    char c;
    while ((c = *str++)) write(c);
}

void VGAout::printPGM(uint16_t x, uint16_t y, const char str[]) {
    _cursor_x = x; _cursor_y = y;
    printPGM(str);
}

void VGAout::set_cursor(uint16_t x, uint16_t y) {
    _cursor_x = x; 
    _cursor_y = y;
}

void VGAout::print(uint16_t x, uint16_t y, const char str[]) { _cursor_x = x; _cursor_y = y; write(str); }
void VGAout::print(uint16_t x, uint16_t y, char c, int base) { _cursor_x = x; _cursor_y = y; print((long) c, base); }
void VGAout::print(uint16_t x, uint16_t y, unsigned char b, int base) { _cursor_x = x; _cursor_y = y; print((unsigned long) b, base); }
void VGAout::print(uint16_t x, uint16_t y, int n, int base) { _cursor_x = x; _cursor_y = y; print((long) n, base); }
void VGAout::print(uint16_t x, uint16_t y, unsigned int n, int base) { _cursor_x = x; _cursor_y = y; print((unsigned long) n, base); }
void VGAout::print(uint16_t x, uint16_t y, long n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); }
void VGAout::print(uint16_t x, uint16_t y, unsigned long n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); }
void VGAout::print(uint16_t x, uint16_t y, double n, int digits) { _cursor_x = x; _cursor_y = y; print(n, digits); }

void VGAout::println(uint16_t x, uint16_t y, const char c[]) { _cursor_x = x; _cursor_y = y; print(c); println(); }
void VGAout::println(uint16_t x, uint16_t y, char c, int base) { _cursor_x = x; _cursor_y = y; print(c, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, unsigned char b, int base) { _cursor_x = x; _cursor_y = y; print(b, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, int n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, unsigned int n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, long n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, unsigned long n, int base) { _cursor_x = x; _cursor_y = y; print(n, base); println(); }
void VGAout::println(uint16_t x, uint16_t y, double n, int digits) { _cursor_x = x; _cursor_y = y; print(n, digits); println(); }

// Print Base Integer Formatter
void VGAout::printNumber(unsigned long n, uint8_t base) {
    unsigned char buf[8 * sizeof(long)]; 
    unsigned long i = 0;

    if (n == 0) {
        print('0');
        return;
    } 

    while (n > 0) {
        buf[i++] = n % base;
        n /= base;
    }

    for (; i > 0; i--) {
        print((char) (buf[i - 1] < 10 ? '0' + buf[i - 1] : 'A' + buf[i - 1] - 10));
    }
}

// Print Floating-point Formatter
void VGAout::printFloat(double number, uint8_t digits) { 
    if (number < 0.0) {
        print('-');
        number = -number;
    }

    double rounding = 0.5;
    for (uint8_t i = 0; i < digits; ++i) rounding /= 10.0;
    number += rounding;

    unsigned long int_part = (unsigned long)number;
    double remainder = number - (double)int_part;
    print(int_part);

    if (digits > 0) print("."); 

    while (digits-- > 0) {
        remainder *= 10.0;
        int toPrint = int(remainder);
        print(toPrint);
        remainder -= toPrint; 
    } 
}

// Tone Audio Stub Routines (Bypassed to prevent system interrupt collisions)
void VGAout::tone(uint16_t freq, uint16_t duration) {}
void VGAout::noTone() {}