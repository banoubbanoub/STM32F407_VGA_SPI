// =============================================================================
// FILE: VGA.cpp
// SYSTEM: STM32F407 VGA 640x480 @ 60Hz Driver (I2S2 Transmit Mode)
// ARCHITECTURE: TIM4 Sync Engine + I2S2 MOSI Output + DMA1 Stream 4 (Channel 0)
// =============================================================================

#include "VGA.h"
#include "stm32f407xx.h"
#include "stm32f4xx_hal.h"

// =============================================================================
// FILE: VGA.cpp
// SYSTEM: STM32F407 VGA 640x480 @ 60Hz Driver (I2S2 Transmit Mode)
// ARCHITECTURE: TIM4 Sync Engine + I2S2 MOSI Output + DMA1 Stream 4 (Channel 0)
// =============================================================================

#include "VGA.h"
#include "stm32f407xx.h"
#include "stm32f4xx_hal.h"

static inline void VGA_Led(uint32_t pin, bool on) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    GPIOD->MODER = (GPIOD->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIOD->BSRR = on ? (1U << pin) : (1U << (pin + 16));
}

extern "C" void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {};
    RCC_ClkInitTypeDef clk = {};
    RCC_PeriphCLKInitTypeDef periphClk = {};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;
    HAL_StatusTypeDef ok = HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (ok == HAL_OK) ok = HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5);
    VGA_Led(12, ok == HAL_OK);

    periphClk.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    periphClk.PLLI2S.PLLI2SN = 252;
    periphClk.PLLI2S.PLLI2SR = 2;
    VGA_Led(13, HAL_RCCEx_PeriphCLKConfig(&periphClk) == HAL_OK);
}

static volatile uint16_t vga_line = 0;

static inline void VGA_VideoOff() {
    SPI2->I2SCFGR &= ~SPI_I2SCFGR_I2SE;
    GPIOB->BSRR = GPIO_BSRR_BR15;   
    GPIOB->MODER = (GPIOB->MODER & ~(3U << 30)) | (1U << 30);
}

VGA_class VGA;

extern "C" __attribute__((weak)) int _write(int, char*, int len) { return len; }

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

uint8_t* VGA_class::VRAM() {
    return vram_front;
}

void VGA_class::cls() {
    if (vram_front) {
        memset(vram_front, 0, VGA_VRAM_SIZE);
    }
}

// =============================================================================
// Non-blocking DMA Start
// =============================================================================
static inline void VGA_StartDMA(uint8_t *buffer) {
    // Force reset DMA Stream without blocking
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;
    
    // Clear DMA Flags
    DMA1->HIFCR = DMA_HIFCR_CFEIF4 | DMA_HIFCR_CDMEIF4 | 
                  DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4  | DMA_HIFCR_CTCIF4;

    DMA1_Stream4->M0AR = (uint32_t)buffer;
    DMA1_Stream4->NDTR = VGA_BYTES_PER_LINE / 2; // 16-bit half-words

    DMA1_Stream4->CR |= DMA_SxCR_EN;
    SPI2->CR2 |= SPI_CR2_TXDMAEN;

    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SE;
    GPIOB->MODER = (GPIOB->MODER & ~(3U << 30)) | (2U << 30);
}

extern "C" void TIM4_IRQHandler(void) {
    VGA.VGA_TIM4_Handler();
}

void VGA_class::VGA_TIM4_Handler() {
    uint32_t sr = TIM4->SR;

    if (sr & TIM_SR_UIF) {
        TIM4->SR = ~TIM_SR_UIF; // Clear Update Flag

        VGA_VideoOff();

        // VSYNC Pulse logic (active LOW on lines 490 & 491)
        if (vga_line >= 490 && vga_line <= 491) {
            GPIOB->BSRR = GPIO_BSRR_BR7; 
        } else {
            GPIOB->BSRR = GPIO_BSRR_BS7; 
        }

        vga_line++;
        if (vga_line >= 525) {
            vga_line = 0;
            static uint8_t frames = 0;
            if (++frames >= 30) {   
                frames = 0;
                GPIOD->ODR ^= (1U << 15);
            }
        }
    }

    if (sr & TIM_SR_CC2IF) {
        TIM4->SR = ~TIM_SR_CC2IF; // Clear Compare Flag

        if (vga_line < VGA_HEIGHT) {
            uint16_t y = vga_line;
            uint8_t *line_ptr = vram_front + (y * VGA_BYTES_PER_LINE);
            VGA_StartDMA(line_ptr);
        }
    }
}

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
    VGA_Led(15, false);
    initI2S2();
    initDMA();
    initTIM4();

    NVIC_SetPriority(SysTick_IRQn, 15);
    NVIC_SetPriority(TIM4_IRQn, 0);
    NVIC_SetPriority(DMA1_Stream4_IRQn, 1);
    VGA_Led(14, true);
}

void VGA_class::end() {
    TIM4->CR1 &= ~TIM_CR1_CEN;
    VGA_VideoOff();
    DMA1_Stream4->CR &= ~DMA_SxCR_EN;

    vram_front = nullptr;
}

void VGA_class::initGPIO() {
    // Enable GPIOB & GPIOD clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIODEN;

    // PB6 -> TIM4_CH1 (HSYNC, AF2)
    GPIOB->MODER &= ~(3U << (6 * 2));
    GPIOB->MODER |=  (2U << (6 * 2));
    GPIOB->AFR[0] &= ~(0xF << (6 * 4));
    GPIOB->AFR[0] |=  (2U << (6 * 4)); 
    GPIOB->OSPEEDR |= (3U << (6 * 2));

    // PB7 -> VSYNC Output (Push-pull GPIO)
    GPIOB->MODER &= ~(3U << (7 * 2));
    GPIOB->MODER |=  (1U << (7 * 2));
    GPIOB->OSPEEDR |= (3U << (7 * 2));
    GPIOB->BSRR = GPIO_BSRR_BS7;

    // PB15 -> I2S2_SD (AF5)
    GPIOB->MODER &= ~(3U << (15 * 2));
    GPIOB->MODER |=  (1U << (15 * 2));
    GPIOB->BSRR = GPIO_BSRR_BR15;
    GPIOB->AFR[1] &= ~(0xF << ((15 - 8) * 4));
    GPIOB->AFR[1] |=  (5U << ((15 - 8) * 4));
    GPIOB->OSPEEDR |= (3U << (15 * 2));
}

void VGA_class::initI2S2() {
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    SPI2->I2SCFGR = 0;
    SPI2->I2SCFGR |= SPI_I2SCFGR_I2SMOD | (2 << SPI_I2SCFGR_I2SCFG_Pos);
    SPI2->I2SPR = 2 | SPI_I2SPR_ODD; 
    SPI2->CR2 |= SPI_CR2_TXDMAEN;
}

void VGA_class::initDMA() {
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;

    DMA1_Stream4->CR &= ~DMA_SxCR_EN;

    DMA1->HIFCR = DMA_HIFCR_CFEIF4 | DMA_HIFCR_CDMEIF4 | 
                  DMA_HIFCR_CTEIF4 | DMA_HIFCR_CHTIF4  | DMA_HIFCR_CTCIF4;

    DMA1_Stream4->PAR = (uint32_t)&(SPI2->DR);
    DMA1_Stream4->CR  = (0 << DMA_SxCR_CHSEL_Pos) | 
                        DMA_SxCR_DIR_0            | 
                        DMA_SxCR_MINC             | 
                        DMA_SxCR_MSIZE_0          | 
                        DMA_SxCR_PSIZE_0          | 
                        DMA_SxCR_PL_1;
}

void VGA_class::initTIM4() {
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    TIM4->CR1 = 0;
    TIM4->PSC = 0;
    TIM4->ARR = VGA_ARR;
    TIM4->CCR1 = VGA_HSYNC_TICKS;
    TIM4->CCR2 = VGA_VIDEO_START;

    // Configure PWM Mode 1 on TIM4 Channel 1
    TIM4->CCMR1 &= ~(TIM_CCMR1_OC1M_Msk | TIM_CCMR1_OC1PE);
    TIM4->CCMR1 |=  (6 << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
    
    // Enable Channel 1 output, set inverted polarity (Active LOW HSYNC)
    TIM4->CCER |= TIM_CCER_CC1E | TIM_CCER_CC1P;

    TIM4->DIER = TIM_DIER_UIE | TIM_DIER_CC2IE;
    TIM4->CR1 |= TIM_CR1_ARPE;

    TIM4->EGR = TIM_EGR_UG;
    TIM4->SR = 0;

    NVIC_SetPriority(TIM4_IRQn, 0);
    NVIC_EnableIRQ(TIM4_IRQn);

    TIM4->CR1 |= TIM_CR1_CEN;
}

extern "C" void DMA1_Stream4_IRQHandler(void) {
    uint32_t flags = DMA1->HISR;

    if (flags & DMA_HISR_TCIF4) {
        DMA1->HIFCR = DMA_HIFCR_CTCIF4;
        SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
        SPI2->DR = 0x0000;
    }
    if (flags & DMA_HISR_TEIF4) {
        DMA1->HIFCR = DMA_HIFCR_CTEIF4;
        SPI2->DR = 0x0000;
        SPI2->CR2 &= ~SPI_CR2_TXDMAEN;
    }
}