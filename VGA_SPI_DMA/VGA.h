#ifndef __VGA_H__
#define __VGA_H__

#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>

// =============================================================================
// VGA 640x480 @ 60 Hz TIMING SPECIFICATIONS (Rendered as 640x480 @ 60 Hz)
// =============================================================================
// Pixel Clock Target     = 25.0 MHz (Derived via SPI Prescaler)
// Horizontal Scan Rate   = 31.25 kHz (~31.468 kHz standard)
// Framebuffer Res        = 640 x 480 Monochrome (1 byte/pixel = 256 green levels)
// Framebuffer Size       = 640 * 480 = 307,200 Bytes (307.2 KB in internal SRAM)
//
// Hardware Pin Mapping:
// - PB6: TIM4_CH1 (PWM)  -> HSYNC (VGA Pin 13, 31.25 kHz active LOW)
// - PB7: GPIO Output     -> VSYNC (VGA Pin 14, 60 Hz active LOW)
// - PA7: SPI1 MOSI       -> Green Video Out (VGA Pin 2 via ~270 ohm resistor)
// =============================================================================

// 1 bpp framebuffer (as expected by Engine3D): 80 bytes/line, 38,400 bytes.
// SYSCLK is 201.6 MHz so SPI1 (APB2 100.8 MHz / 4) = 25.2 MHz = VGA pixel clock.
#define VGA_WIDTH            640
#define VGA_HEIGHT           480
#define VGA_BYTES_PER_LINE   (VGA_WIDTH / 8)
#define VGA_VRAM_SIZE        (VGA_BYTES_PER_LINE * VGA_HEIGHT)

// TIM4 clock = 100.8 MHz: 3203 ticks = 31.78 us line (31.47 kHz)
#define VGA_ARR              3202
#define VGA_HSYNC_TICKS      384     // 3.81 us HSync pulse
#define VGA_VIDEO_START      520     // Back porch (5.7 us) minus ISR/DMA latency; tune to shift image

class VGA_class {
public:
    VGA_class();
    void begin(uint8_t* extram = nullptr);
    void end();
    
    uint8_t* VRAM();
    void cls();
    

       /**
     * @brief Pauses execution for a specified number of vertical frame refreshes (~60Hz).
     * @param frames Number of frames to wait.
     */
    void delay_frame(uint16_t frames);

    // Blocks until the vertical blanking interval has started.
    void waitVBlank();

    /**
     * @brief Registers a hook callback called at the start of the vertical blanking interval.
     */
    void setBktmStartHook(void (*func)());

    /**
     * @brief Registers a hook callback called at the end of the vertical blanking interval.
     */
    void setBktmEndHook(void (*func)());

    // Display Dimension Getters
    uint16_t width() { return VGA_WIDTH; }
    uint16_t height() { return VGA_HEIGHT; }
    uint16_t vram_size() { return VGA_VRAM_SIZE; }

    /**
     * @brief Allocates dynamic memory for double-buffering to prevent visual tearing.
     * @return true if memory allocation succeeded, false otherwise.
     */
    bool initDoubleBuffer();

    /**
     * @brief Swaps the front and back buffers during vertical blanking.
     * @param copy_to_back If true, copies the new front buffer content into the back buffer.
     */
    void swap(bool copy_to_back = false);

    /**
     * @brief Returns pointer to the back buffer when double-buffered, or front buffer otherwise.
     */
    uint8_t* getBackBuffer() { return double_buffered ? vram_back : VRAM(); }

    /**
     * @brief Returns true if double-buffering is currently enabled.
     */
    bool isDoubleBuffered() { return double_buffered; }

    void VGA_TIM4_Handler();

private:

uint8_t* vram_front;          // Pointer to active front display buffer
    uint8_t* vram_back;           // Pointer to back draw buffer (for double-buffering)
    bool external_vram;           // Flag indicating if external memory is used for VRAM
    bool double_buffered;         // Flag indicating if double-buffering mode is active

    void (*blank_start_hook)();   // User callback executed at start of V-blank
    void (*blank_end_hook)();     // User callback executed at end of V-blank

    void initGPIO();
    void initSPI1();
    void initDMA();
    void initTIM4();

 
};

extern VGA_class VGA;
#endif // __VGA_H__

// #endif // __VGA_H__


