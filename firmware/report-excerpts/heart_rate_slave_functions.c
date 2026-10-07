/*
 * Historical code excerpts from the project report, PDF pages 11-19.
 * Formatting restored from the PDF; original register logic retained.
 * ADC sample-time and DMA priority comments corrected against ST RM0008.
 * This is not a complete firmware translation unit. See ../README.md.
 */

/* Report p. 11: SPI 초기화 */
void SPI2_Slave_Init_Register(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;

    // CRH 초기화 (12~15번 핀)
    GPIOB->CRH &= 0x0000FFFF;
    // PB12(NSS), PB13(SCK), PB15(MOSI) -> Input Floating (0x4)
    // PB14(MISO) -> AF Push-Pull (0xB)
    // 순서: 15(4), 14(B), 13(4), 12(4)
    GPIOB->CRH |= 0x4B440000;

    // SPI2 설정
    SPI2->CR1 = 0;
    SPI2->CR1 &= ~SPI_CR1_SPE; // Disable
    // Slave Mode, HW NSS, Mode 0 (CPOL=0, CPHA=0)
    SPI2->CR1 &= ~(SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI);
    SPI2->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);
    SPI2->CR1 |= SPI_CR1_SPE; // Enable
}

/* Report p. 12: GPIO 초기화 */
void GPIO_Init_Register(void)
{
    /* --- 1. GPIOA / GPIOB Clock Enable --- */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN; // Enable GPIOA clock
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN; // Enable GPIOB clock

    // Clear PB3/PB4/PB5 config bits
    GPIOB->CRL &= ~(0x00FFF000);
    // Set MODE=10, CNF=00 → 0b0010 = 0x2 (2MHz output PP)
    GPIOB->CRL |= (0x00222000); // PB3=2, PB4=2, PB5=2

    GPIOB->CRH &= ~(0x000F0000); // Clear PB12
    GPIOB->CRH |= (0x00080000); // MODE=00, CNF=10 → input pull-up/down
    // Pull-up enable
    GPIOB->BSRR = GPIO_BSRR_BS12;

    GPIOA->CRL &= ~(0x00F00000); // Clear PA5 config
    GPIOA->CRL |= (0x00200000); // MODE=10, CNF=00
}

/* Report pp. 13-14: Calibration 설정 */
void ADC1_Calibration_Register(void)
{
    // ADON 켜서 ADC 활성화
    ADC1->CR2 |= ADC_CR2_ADON;
    // 잠깐 대기 (전원 안정화용)
    for (volatile int i = 0; i < 1000; i++)
    {
        __NOP();
    }
    // 캘리브레이션 리셋
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    while (ADC1->CR2 & ADC_CR2_RSTCAL)
    {
        // RSTCAL 클리어될 때까지 대기
    }
    // 캘리브레이션 시작
    ADC1->CR2 |= ADC_CR2_CAL;
    while (ADC1->CR2 & ADC_CR2_CAL)
    {
        // CAL 비트가 0 될 때까지 대기
    }
}

/* Report pp. 14-15: ADC1 + DMA 설정 */
void ADC1_Init_Register(void)
{
    // PA0 = Analog Mode (ADC1_IN0)
    GPIOA->CRL &= ~(0xF << 0); // clear PA0 config bits
    GPIOA->CRL |= (0x0 << 0); // MODE=00, CNF=00 → Analog
    //--- 1. ADC Clock Enable ---
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    //--- 2. CR1 설정 ---
    ADC1->CR1 = 0x00000000;
    // Scan mode OFF, Discontinuous OFF → 기본값 0이면 충분

    //--- 3. CR2 설정 ---
    ADC1->CR2 = 0;
    ADC1->CR2 |= ADC_CR2_CONT; // Continuous mode
    ADC1->CR2 |= ADC_CR2_EXTSEL; // Software trigger
    ADC1->CR2 |= ADC_CR2_EXTTRIG; // Trigger enable
    ADC1->CR2 |= ADC_CR2_ADON; // ADC enable (1st ON pulse)
    // 짧은 딜레이 (ADC stabilize용)
    for (volatile int i = 0; i < 1000; i++) __NOP();
    //--- 4. SMPR2: Channel 0 sampling time = 28.5 cycles ---
    // Channel 0 = bits [2:0]
    ADC1->SMPR2 &= ~(0x7); // clear
    ADC1->SMPR2 |= (0x3);
    //--- 5. Regular sequence length = 1 conversion ---
    ADC1->SQR1 &= ~(0x00F00000); // L[3:0] = 0 → 1 conversion
    //--- 6. SQR3: 1st conversion is channel 0 ---
    ADC1->SQR3 &= ~0x1F; // clear SQ1
    ADC1->SQR3 |= 0x00; // channel 0
    ADC1->CR2 |= ADC_CR2_DMA; // DMA enable
    //--- 7. 다시 ADON pulse → conversion start ready ---
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR2 |= ADC_CR2_SWSTART; // conversion start
}

/* Report p. 17: DMA1 채널 1 설정 */
void DMA1_Channel1_Init_Register(uint32_t srcAddr, uint32_t dstAddr)
{
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    // 2) 채널 disable
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    // 3) Peripheral / Memory 주소 설정
    DMA1_Channel1->CPAR = srcAddr; // 보통 (uint32_t)&ADC1->DR
    DMA1_Channel1->CMAR = dstAddr; // 보통 (uint32_t)&adc_val
    // 4) 전송 개수 (1개)
    DMA1_Channel1->CNDTR = 1;
    // 5) DMA 설정
    DMA1_Channel1->CCR =
          DMA_CCR_PSIZE_0 // Peripheral size = 16bit
        | DMA_CCR_MSIZE_0 // Memory size = 16bit
        | DMA_CCR_CIRC    // Circular mode
        | DMA_CCR_PL_0    // Priority Medium
        | DMA_CCR_TCIE;   // Transfer complete interrupt enable
    // 6) 채널 enable
    DMA1_Channel1->CCR |= DMA_CCR_EN;
}

/* Report p. 19: ADC 값 Scaling */
void ADC_DMA_Update(void)
{
    uint16_t v = adc_val;
    int s;
    if (v < 2400)
        s = (v * 50) / 2400;
    else if (v < 2500)
        s = 50 + (v - 2400) / 20;
    else if (v < 2700)
        s = 60 + ((v - 2500) * 40) / 200;
    else if (v <= 3000)
        s = 100 + ((v - 2700) * 40) / 300;
    else
        s = 150;
    scaled = s;
}
