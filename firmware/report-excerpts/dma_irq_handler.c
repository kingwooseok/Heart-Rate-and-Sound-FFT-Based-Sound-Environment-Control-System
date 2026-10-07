/*
 * Historical excerpt from the project report, PDF p. 18.
 * The report places this handler in stm32f1xx_it.c.
 * Headers, ADC_DMA_Update declaration and NVIC setup are not supplied here.
 * See ../README.md.
 */
void DMA1_Channel1_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TCIF1)
    {
        DMA1->IFCR = DMA_IFCR_CTCIF1; // Clear interrupt flag
        ADC_DMA_Update(); // main.c 함수 호출
    }
}
