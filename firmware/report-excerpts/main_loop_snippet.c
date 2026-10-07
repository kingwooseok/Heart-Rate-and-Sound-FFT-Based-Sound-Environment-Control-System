/*
 * Historical main-loop fragment from the project report, PDF pp. 19-20.
 * Not a standalone C translation unit: this belongs inside the original main().
 * The isolated '-' printed immediately after '{' on p. 19 was removed.
 * Missing declarations/functions are listed in ../README.md.
 */
while (1)
{
    if (SPI2->SR & SPI_SR_RXNE)
    {
        uint8_t rx = SPI2_Slave_TransmitReceive((uint8_t)(scaled & 0xFF));
    }

    // PB3/PB4/PB5 모두 LOW 만들기
    GPIOB->BRR = (GPIO_BRR_BR3 | GPIO_BRR_BR4 | GPIO_BRR_BR5);
    if (scaled <= 60)
        GPIOB->BSRR = GPIO_BSRR_BS3;
    else if (scaled <= 110)
        GPIOB->BSRR = GPIO_BSRR_BS4;
    else
        GPIOB->BSRR = GPIO_BSRR_BS5;

    /* -----------------------------
       3. UART Debug 출력
       ----------------------------- */
    int len = sprintf(buf, "ADC=%u   HR=%d\r\n", adc_val, scaled);
    HAL_UART_Transmit(&huart2, (uint8_t*)buf, len, 100);
    HAL_Delay(200);
}
