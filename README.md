# STM32F407 VGA 640×480 @ 60 Hz Driver

A high-speed **640×480 @ 60 Hz VGA driver for the STM32F407**, using a hardware timer for video timing and DMA-driven serial output for pixel data.

This project was developed as a low-level VGA experiment using the STM32F407 peripherals directly. The driver can generate VGA video using either:

* **SPI2 transmit mode**
* **I2S2 transmit mode**

Both approaches use DMA to continuously feed pixel data to the output peripheral.

---

## Features

* STM32F407 @ 168 MHz
* 640×480 VGA
* ~60 Hz refresh
* Hardware-generated HSYNC
* Software-controlled VSYNC
* TIM4 video timing engine
* DMA-driven pixel transmission
* External or internal framebuffer support
* 1-bit-per-pixel framebuffer
* SPI2 and I2S2 output experiments
* No software bit-banging for pixel transmission
* Designed for Arduino/STM32 environments
* Direct register-level peripheral configuration

---

# Video Architecture

The driver divides the VGA system into three main hardware blocks:

```text
                    STM32F407
                ┌─────────────────┐
                │                 │
                │     TIM4        │
                │                 │
                │  Line Timing    │
                └───────┬─────────┘
                        │
              ┌─────────┴─────────┐
              │                   │
              ▼                   ▼
          HSYNC PB6          VSYNC PB7
          TIM4 CH1            GPIO
              │                   │
              │                   │
              └────────┬──────────┘
                       │
                       │
                VGA connector
                       │
                       │
                 ┌─────┴─────┐
                 │           │
                 ▼           ▼
              Sync       Pixel Data
                            │
                       ┌────┴────┐
                       │         │
                    SPI2       I2S2
                       │         │
                       └────┬────┘
                            │
                         DMA1
                            │
                            ▼
                         PB15
                            │
                       VGA video
```

The CPU does not need to manually toggle the pixel output for every pixel.

Instead:

1. TIM4 generates the horizontal timing.
2. TIM4 reaches the video-start position.
3. A DMA transfer begins.
4. DMA sends a complete scanline to SPI2/I2S2.
5. The serial peripheral generates the pixel clock.
6. The next TIM4 interrupt starts the next line.

This keeps the CPU load relatively low while the hardware handles the high-speed pixel stream.

---

# VGA Timing

The target mode is:

```text
Resolution:       640 × 480
Refresh:          ~60 Hz
Total lines:      525
Visible lines:    480
Horizontal rate:  ~31.5 kHz
```

A VGA frame contains:

```text
525 total lines
│
├── Vertical blanking
│
├── 480 visible lines
│
└── Vertical blanking
```

The driver keeps a line counter:

```cpp
static volatile uint16_t vga_line = 0;
```

The counter runs from:

```text
0 ... 524
```

and then returns to:

```text
0
```

---

# TIM4 Video Timing Engine

TIM4 is the main timing generator.

The timer is configured with:

```cpp
TIM4->PSC = 0;
TIM4->ARR = VGA_ARR;
```

The timer update event represents the beginning of a new VGA scanline.

TIM4 Channel 1 generates HSYNC:

```cpp
TIM4->CCR1 = VGA_HSYNC_TICKS;
```

Channel 1 is configured for PWM output:

```cpp
TIM4->CCMR1 |=
    (6 << TIM_CCMR1_OC1M_Pos) |
    TIM_CCMR1_OC1PE;

TIM4->CCER |=
    TIM_CCER_CC1E |
    TIM_CCER_CC1P;
```

The inverted polarity produces an active-low VGA sync signal.

### HSYNC

Output pin:

```text
PB6
```

Peripheral:

```text
TIM4_CH1
```

Alternate function:

```text
AF2
```

---

# VSYNC

VSYNC is generated using GPIO PB7.

The driver generates the vertical sync pulse during the appropriate lines:

```cpp
if (vga_line >= 490 && vga_line <= 491) {
    GPIOB->BSRR = GPIO_BSRR_BR7;
} else {
    GPIOB->BSRR = GPIO_BSRR_BS7;
}
```

Therefore:

```text
PB7 = VSYNC
```

The signal is active LOW.

---

# Pixel Output

The driver supports two closely related output methods.

## SPI2 Mode

SPI2 can be used as a high-speed serial pixel transmitter.

The framebuffer data is transferred through:

```text
SPI2->DR
```

with DMA.

The output pin is:

```text
PB15
```

---

# I2S2 Mode

The same SPI2 peripheral can operate in I2S transmit mode.

I2S is enabled with:

```cpp
SPI2->I2SCFGR =
    SPI_I2SCFGR_I2SMOD |
    (2 << SPI_I2SCFGR_I2SCFG_Pos);
```

The I2S clock is configured using:

```cpp
SPI2->I2SPR = 2 | SPI_I2SPR_ODD;
```

The output is again:

```text
PB15
```

This is an interesting feature of the STM32F407: **SPI2 and I2S2 share the same physical peripheral**, allowing the same DMA-based architecture to be experimented with using either serial mode.

---

# DMA Architecture

DMA1 Stream 4 is used for pixel transmission.

```text
DMA1
 └── Stream 4
      └── Channel 0
           └── SPI2/I2S2 TX
```

The peripheral address is:

```cpp
DMA1_Stream4->PAR = (uint32_t)&SPI2->DR;
```

The DMA direction is:

```text
Memory → Peripheral
```

and memory increment is enabled:

```cpp
DMA_SxCR_MINC
```

The DMA transfer uses 16-bit values:

```cpp
DMA_SxCR_MSIZE_0
DMA_SxCR_PSIZE_0
```

---

# Scanline DMA

When TIM4 reaches the configured video-start point:

```cpp
if (vga_line < VGA_HEIGHT) {
    uint16_t y = vga_line;

    uint8_t *line_ptr =
        vram_front + (y * VGA_BYTES_PER_LINE);

    VGA_StartDMA(line_ptr);
}
```

The corresponding framebuffer line is sent automatically by DMA.

The CPU therefore does not need to execute a loop such as:

```cpp
for each pixel
    send pixel;
```

Instead:

```text
Framebuffer
     │
     ▼
DMA
     │
     ▼
SPI2/I2S2
     │
     ▼
PB15
     │
     ▼
VGA
```

---

# DMA Completion

When the scanline transfer finishes, the DMA interrupt is generated:

```cpp
extern "C" void DMA1_Stream4_IRQHandler(void)
```

The transfer-complete flag is checked:

```cpp
if (flags & DMA_HISR_TCIF4)
```

Then the DMA request is disabled:

```cpp
SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
```

The output is returned to zero:

```cpp
SPI2->DR = 0x0000;
```

This helps prevent unwanted pixel output outside the active video region.

---

# Framebuffer

The driver supports an internal framebuffer:

```cpp
static uint8_t
vram_storage[VGA_VRAM_SIZE]
__attribute__((aligned(4)));
```

or an externally supplied framebuffer:

```cpp
void VGA_class::begin(uint8_t* extram)
```

For example:

```cpp
VGA.begin();
```

uses internal RAM.

External memory can be supplied with:

```cpp
VGA.begin(externalBuffer);
```

This makes the driver suitable for boards using external SRAM.

---

# Framebuffer Layout

The framebuffer is organized as scanlines:

```text
Framebuffer
┌─────────────────────────────┐
│ Line 0                      │
├─────────────────────────────┤
│ Line 1                      │
├─────────────────────────────┤
│ Line 2                      │
├─────────────────────────────┤
│ ...                         │
├─────────────────────────────┤
│ Line 479                    │
└─────────────────────────────┘
```

The address of a scanline is calculated as:

```cpp
line_ptr =
    vram_front +
    (y * VGA_BYTES_PER_LINE);
```

This makes it possible to send exactly one framebuffer line per VGA line.

---

# GPIO Pinout

| Signal | STM32F407 Pin | Function       |
| ------ | ------------- | -------------- |
| HSYNC  | PB6           | TIM4_CH1       |
| VSYNC  | PB7           | GPIO           |
| VIDEO  | PB15 / PA7    | SPI2/I2S2 data |
| LED    | PD12–PD15     | Debug/status   |

The video signal on PB15 should be connected to the VGA circuit through the appropriate resistor network.

For a normal VGA interface, the STM32 should **not** be connected directly to a 75 Ω VGA input without considering the required voltage levels and termination.

---

# VGA Signal

A typical VGA connection contains:

```text
STM32F407
    │
    ├── PB6  ──► HSYNC
    │
    ├── PB7  ──► VSYNC
    │
    └── PB15/PA7 ──► VIDEO
                   │
                resistor
                   │
                   ▼
                 VGA
```

For RGB VGA, separate RGB channels are normally required. This particular implementation is primarily intended as a **single-channel digital video experiment**, where PB15/PA7 carries the pixel stream.

A resistor DAC or appropriate video-level interface can be added if multiple brightness/color levels are required.

---

# Initialization

The basic initialization sequence is:

```cpp
VGA.begin();
```

Internally the driver initializes:

```text
Framebuffer
    ↓
GPIO
    ↓
I2S2/SPI2
    ↓
DMA
    ↓
TIM4
    ↓
Interrupts
    ↓
Video output
```

The timer then continuously generates VGA scanlines.

---

# Basic Example

```cpp
#include "VGA.h"

void setup()
{
    VGA.begin();

    VGA.cls();
}

void loop()
{
    VGA.waitVBlank();

    // Draw into VGA.VRAM()
}
```

The framebuffer can be accessed using:

```cpp
uint8_t *vram = VGA.VRAM();
```

---

# Vertical Blank

The driver provides:

```cpp
VGA.waitVBlank();
```

This can be used when software needs to synchronize framebuffer updates with the display.

For example:

```cpp
VGA.waitVBlank();

// Update graphics
```

This is useful for reducing visible tearing when modifying a framebuffer.

---

# Frame Delay

The driver also provides:

```cpp
VGA.delay_frame(60);
```

which waits for the requested number of frames.

Example:

```cpp
VGA.delay_frame(30);
```

approximately waits half a second when running at 60 FPS.

---

# Status LEDs

Several LEDs are used during initialization and debugging.

The clock configuration reports success using:

```cpp
VGA_Led(12, ok == HAL_OK);
```

The peripheral clock configuration uses:

```cpp
VGA_Led(13, ...);
```

The VGA driver initialization uses:

```cpp
VGA_Led(14, true);
```

and the frame counter periodically toggles another LED.

These indicators are useful when bringing up the hardware without a debugger.

---

# Why DMA?

Generating VGA pixels entirely in software would require the CPU to perform a very large number of operations at precise intervals.

DMA moves the pixel data automatically:

```text
CPU
 │
 │ configure DMA
 ▼
DMA ───────────────► SPI/I2S
                       │
                       ▼
                     VIDEO
```

The CPU is primarily responsible for:

* framebuffer drawing
* scanline scheduling
* synchronization
* DMA setup

The high-speed data transfer is performed by hardware.

---

# Why I2S?

The STM32F407 I2S peripheral provides another convenient hardware serial clock/data engine.

Because I2S2 is implemented inside the SPI2 peripheral:

```text
SPI2
 │
 ├── SPI mode
 │
 └── I2S mode
```

both modes can use the same:

```text
SPI2->DR
DMA1 Stream 4
PB15
```

This makes the project useful for experimenting with unusual video-output architectures.

In testing, **both SPI2 and I2S2 transmit modes work well with this VGA architecture**.

---

# Performance Architecture

The important design principle is:

```text
                 168 MHz CPU
                      │
                      ▼
                   TIM4
                      │
             ┌────────┴────────┐
             │                 │
           HSYNC           DMA trigger
             │                 │
             ▼                 ▼
            PB6             DMA1 S4
                               │
                               ▼
                          SPI2 / I2S2
                               │
                               ▼
                             PB15
                               │
                               ▼
                              VGA
```

The timer establishes the exact horizontal timing while DMA handles the pixel stream.

This separation is what makes the driver stable at VGA speed.

---

# Important Design Notes

### 1. Do not use blocking DMA waits inside the TIM4 interrupt

The line interrupt must remain short.

The driver starts the DMA transfer and returns to the interrupt system.

### 2. DMA buffer must remain valid

The framebuffer memory cannot be modified or relocated while DMA is reading the active scanline.

### 3. External SRAM

An external SRAM framebuffer can be used, but its access speed and bus timing must be sufficient for the required scanline bandwidth.

### 4. VGA levels

The STM32 GPIO is a digital 3.3 V output. A proper resistor network should be used to produce VGA-compatible signal levels.

### 5. Interrupt priority

TIM4 has the highest priority:

```cpp
NVIC_SetPriority(TIM4_IRQn, 0);
```

DMA is slightly lower:

```cpp
NVIC_SetPriority(DMA1_Stream4_IRQn, 1);
```

This keeps scanline timing deterministic.

---

# Project Structure

A typical project can be organized as:

```text
STM32F407-VGA/
│
├── VGA.h
├── VGA.cpp
├── main.cpp
├── README.md
│
└── platformio.ini
```

---

# Hardware

Recommended hardware:

* STM32F407 development board
* VGA connector
* Resistor 180 ohm network for video output
* VGA monitor capable of accepting 640×480 @ 60 Hz

An oscilloscope or logic analyzer is highly recommended during development.

The most useful signals to probe are:

```text
PB6  → HSYNC
PB7  → VSYNC
PB15 → Pixel clock/data stream
```

---

# Current Status

**Working**

* [x] STM32F407 clock configuration
* [x] TIM4 horizontal timing
* [x] HSYNC output
* [x] VSYNC generation
* [x] DMA scanline transmission
* [x] SPI2 transmit mode
* [x] I2S2 transmit mode
* [x] Internal framebuffer
* [x] External framebuffer support
* [x] 640×480 VGA output
* [x] ~60 Hz refresh

---

# Future Development

Possible improvements include:

* [ ] Multiple RGB color channels
* [ ] 2-bit / 4-bit / 8-bit color
* [ ] External SRAM framebuffer
* [ ] Double buffering
* [ ] Hardware-accelerated drawing
* [ ] Text rendering
* [ ] Bitmap rendering
* [ ] Sprite engine
* [ ] Hardware scrolling
* [ ] Higher resolutions
* [ ] SPI/I2S automatic mode selection
* [ ] More precise VGA timing measurements

---

# License

Add your preferred open-source license here, for example MIT:

```text
MIT License
```

---

# Author

**Banou**

STM32 / embedded graphics / VGA experimentation.

This project is primarily an exploration of using the STM32F407's hardware peripherals — timers, DMA, SPI and I2S — as a real-time video engine.
