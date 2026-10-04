// =============================================================================
// FILE: VGA.cpp
// SYSTEM: STM32F407 VGA 640x480 @ 60Hz Video Driver (Green Monochrome / 8-bit)
// ARCHITECTURE: TIM4 HSYNC Pulse Engine + SPI1 Pixel Shift + DMA2 Stream 3
// =============================================================================

#include "VGA.h"
#include "stm32f407xx.h"
#include "stm32f4xx_hal.h"

// Overclocked: 8 MHz HSE /5 *252 /2 = 201.6 MHz; APB1 /4, APB2 /2 (timers and SPI1 at 100.8 MHz).
extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 5;
    osc.PLL.PLLN = 252;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 9;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7);
}

// -----------------------------------------------------------------------------
// PRIVATE VOLATILE STATE VARIABLES
// -----------------------------------------------------------------------------
static volatile uint16_t vga_line = 0;   // Current line counter (0 to 524)
static volatile bool dma_active = false;

VGA_class VGA;

// Satisfies newlib-nano when the core's syscalls.o is not pulled in by the linker.
extern "C" __attribute__((weak)) int _write(int, char*, int len) { return len; }

// Static buffer: 307 KB malloc cannot succeed on 192 KB of SRAM.
static uint8_t vram_storage[VGA_VRAM_SIZE] __attribute__((aligned(4)));

VGA_class::VGA_class() 
    : vram_front(nullptr), vram_back(nullptr), external_vram(false), double_buffered(false),
      blank_start_hook(nullptr), blank_end_hook(nullptr) {}

void VGA_class::waitVBlank() {
    while (vga_line < VGA_HEIGHT);
}


void VGA_class::delay_frame(uint16_t frames) {
    while (frames--) {
        while (vga_line >= VGA_HEIGHT);
        while (vga_line < VGA_HEIGHT);
    }
}
//
uint8_t* VGA_class::VRAM() {
    return vram_front;
}

void VGA_class::cls() {
    if (vram_front) {
        memset(vram_front, 0, VGA_VRAM_SIZE);
    }
}

// =============================================================================
// Start DMA Stream: Sends 320 bytes over SPI1 to PA7
// =============================================================================
static inline void VGA_StartDMA(uint8_t *buffer) {
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    DMA2_Stream3->M0AR = (uint32_t)buffer;
    DMA2_Stream3->NDTR = VGA_BYTES_PER_LINE;

    dma_active = true;
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

// =============================================================================
// INTERRUPT HANDLER: TIM4 Interrupt Vector Routing
// =============================================================================
extern "C" void TIM4_IRQHandler(void) {
    VGA.VGA_TIM4_Handler();
}

// =============================================================================
// TVGA_TIM4_Handler: Horizontal Line Scan Engine (Fired @ 31.25 kHz)
// =============================================================================
void VGA_class::VGA_TIM4_Handler() {
    uint32_t sr = TIM4->SR;

    // -------------------------------------------------------------------------
    // EVENT 1: Line Start - Update VSYNC Pin State & Advance Line Counter
    // -------------------------------------------------------------------------
    if (sr & TIM_SR_UIF) {
        TIM4->SR = ~TIM_SR_UIF;

        // VSYNC signal handling (VGA active LOW pulse on Lines 490 and 491)
        if (vga_line >= 490 && vga_line <= 491) {
            GPIOB->BSRR = GPIO_BSRR_BR7; // VSYNC LOW
        } else {
            GPIOB->BSRR = GPIO_BSRR_BS7; // VSYNC HIGH
        }

        vga_line++;
        if (vga_line >= 525) { // 525 total lines per frame in 640x480 VGA
            vga_line = 0;
        }
    }

    // -------------------------------------------------------------------------
    // EVENT 2: Active Raster Start - Trigger Video DMA Shift
    // -------------------------------------------------------------------------
    if (sr & TIM_SR_CC2IF) {
        TIM4->SR = ~TIM_SR_CC2IF;

        // Active video window: 480 visible scanlines
        if (vga_line < VGA_HEIGHT) {
            // Divide by 2 to double lines vertically (240 rows scaled to 480 lines)
            uint16_t y = vga_line;
            uint8_t *line_ptr = vram_front + (y * VGA_BYTES_PER_LINE);
            
            VGA_StartDMA(line_ptr);
        }
    }
}

// =============================================================================
// Subsystem Initializers
// =============================================================================
void VGA_class::begin(uint8_t* extram) {
    if (extram != nullptr) {
        vram_front = extram;
        external_vram = true;
    } else {
        vram_front = vram_storage;
        external_vram = false;
    }
    cls();

    initGPIO();
    initSPI1();
    initDMA();
    initTIM4();

    // SysTick at top priority adds jitter to the line-start ISR.
    NVIC_SetPriority(SysTick_IRQn, 15);
    NVIC_SetPriority(TIM4_IRQn, 0);
    NVIC_SetPriority(DMA2_Stream3_IRQn, 1);
}

void VGA_class::end() {
    TIM4->CR1 &= ~TIM_CR1_CEN;
    SPI1->CR1 &= ~SPI_CR1_SPE;
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;

    vram_front = nullptr;
}

void VGA_class::initGPIO() {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;

    // PB6 -> TIM4_CH1 (HSYNC, AF2)
    GPIOB->MODER &= ~(3U << (6 * 2));
    GPIOB->MODER |=  (2U << (6 * 2));
    GPIOB->AFR[0] &= ~(0xF << (6 * 4));
    GPIOB->AFR[0] |=  (2U << (6 * 4)); 
    GPIOB->OSPEEDR |= (3U << (6 * 2));

    // PB7 -> GPIO Output (VSYNC)
    GPIOB->MODER &= ~(3U << (7 * 2));
    GPIOB->MODER |=  (1U << (7 * 2)); // General Purpose Output
    GPIOB->OSPEEDR |= (3U << (7 * 2));
    GPIOB->BSRR = GPIO_BSRR_BS7;      // Default HIGH

    // PA7 -> SPI1_MOSI (Green DAC Output, AF5)
    GPIOA->MODER &= ~(3U << (7 * 2));
    GPIOA->MODER |=  (2U << (7 * 2));
    GPIOA->AFR[0] &= ~(0xF << (7 * 4));
    GPIOA->AFR[0] |=  (5U << (7 * 4));
    GPIOA->OSPEEDR |= (3U << (7 * 2));
}

void VGA_class::initSPI1() {
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    SPI1->CR1 = 0;
    // APB2 = 84 MHz / 4 = 21.0 MHz SPI clock (close to 25.175 MHz standard)
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_BR_0 | SPI_CR1_SSI | SPI_CR1_SSM;
    SPI1->CR2 = SPI_CR2_TXDMAEN;
    SPI1->CR1 |= SPI_CR1_SPE;
}

void VGA_class::initDMA() {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    DMA2->LIFCR = DMA_LIFCR_CFEIF3 | DMA_LIFCR_CDMEIF3 | 
                  DMA_LIFCR_CTEIF3 | DMA_LIFCR_CHTIF3  | DMA_LIFCR_CTCIF3;

    DMA2_Stream3->PAR = (uint32_t)&(SPI1->DR);
    DMA2_Stream3->CR  = (3 << DMA_SxCR_CHSEL_Pos) | 
                        DMA_SxCR_DIR_0             | 
                        DMA_SxCR_MINC              | 
                        DMA_SxCR_PL_1              | 
                        DMA_SxCR_TCIE;

    NVIC_SetPriority(DMA2_Stream3_IRQn, 0);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

void VGA_class::initTIM4() {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    TIM4->CR1 = 0;
    TIM4->PSC = 0;                  // 84 MHz Timer Tick Rate
    TIM4->ARR = VGA_ARR;            // 31.25 kHz scanline rate
    TIM4->CCR1 = VGA_HSYNC_TICKS;   // Channel 1 PWM for HSYNC
    TIM4->CCR2 = VGA_VIDEO_START;   // Channel 2 Compare Interrupt for Video Start

    // TIM4 Channel 1 -> PWM Mode 1 (Inverted for active-LOW sync)
    TIM4->CCMR1 &= ~(TIM_CCMR1_OC1M_Msk | TIM_CCMR1_OC1PE);
    TIM4->CCMR1 |=  (6 << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    TIM4->CCER  |= TIM_CCER_CC1E | TIM_CCER_CC1P;

    TIM4->DIER = TIM_DIER_UIE | TIM_DIER_CC2IE;
    TIM4->CR1 |= TIM_CR1_ARPE;

    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;

    NVIC_SetPriority(TIM4_IRQn, 1);
    NVIC_EnableIRQ(TIM4_IRQn);

    TIM4->CR1 |= TIM_CR1_CEN;
}

// DMA ISR to handle transfer complete
extern "C" void DMA2_Stream3_IRQHandler(void) {
    uint32_t flags = DMA2->LISR;

    if (flags & DMA_LISR_TCIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTCIF3;
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
        SPI1->DR = 0x0000;
        dma_active = false;
    }
    if (flags & DMA_LISR_TEIF3) {
        DMA2->LIFCR = DMA_LIFCR_CTEIF3;
        SPI1->DR = 0x0000;
        SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
        dma_active = false;
    }
}