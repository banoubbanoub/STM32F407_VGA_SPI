#ifndef __VGA_H__
#define __VGA_H__

#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>

// =============================================================================
// VGA 640x480 @ 60 Hz TIMING SPECIFICATIONS (I2S2 Monochromatic Engine)
// =============================================================================
// Pixel Clock Target     = ~25.2 MHz (I2S Clocked via PLLI2S)
// Horizontal Scan Rate   = 31.25 kHz
// Framebuffer Res        = 640 x 480 Monochrome (1 bit per pixel)
// Framebuffer Size       = 640 * 480 / 8 = 38,400 Bytes
//
// Hardware Pin Mapping (STM32F407G-DISC1 Safe):
// - PB6: TIM4_CH1 (PWM)  -> HSYNC (VGA Pin 13)
// - PB7: GPIO Output     -> VSYNC (VGA Pin 14)
// - PB15: I2S2_SD (AF5)   -> Green Video Out (VGA Pin 2 via ~270 ohm resistor)
// =============================================================================

#define VGA_WIDTH            640
#define VGA_HEIGHT           480
#define VGA_BYTES_PER_LINE   (VGA_WIDTH / 8)
#define VGA_VRAM_SIZE        (VGA_BYTES_PER_LINE * VGA_HEIGHT)

// TIM4 clock = 84 MHz: 2669 ticks = 31.77 us line (31.47 kHz)
#define VGA_ARR              2668
#define VGA_HSYNC_TICKS      320     // 3.81 us HSync pulse
#define VGA_VIDEO_START      400     // Back porch (5.7 us) minus ISR/DMA start latency; tune to shift image

class VGA_class {
public:
    VGA_class();
    void begin(uint8_t* extram = nullptr);
    void end();
    
    uint8_t* VRAM();
    void cls();
    
    void delay_frame(uint16_t frames);
    void waitVBlank();

    void setBktmStartHook(void (*func)());
    void setBktmEndHook(void (*func)());

    uint16_t width() { return VGA_WIDTH; }
    uint16_t height() { return VGA_HEIGHT; }
    uint16_t vram_size() { return VGA_VRAM_SIZE; }

    bool initDoubleBuffer();
    void swap(bool copy_to_back = false);
    uint8_t* getBackBuffer() { return double_buffered ? vram_back : VRAM(); }
    bool isDoubleBuffered() { return double_buffered; }

    void VGA_TIM4_Handler();

private:
    uint8_t* vram_front;
    uint8_t* vram_back;
    bool external_vram;
    bool double_buffered;

    void (*blank_start_hook)();
    void (*blank_end_hook)();

    void initGPIO();
    void initI2S2();
    void initDMA();
    void initTIM4();
};

extern VGA_class VGA;
#endif // __VGA_H__