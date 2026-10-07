#ifndef HR_TEST_STM32F1XX_HAL_H
#define HR_TEST_STM32F1XX_HAL_H

/* Host-only test doubles. These structures are not an STM32 HAL replacement.
 * API signatures match STM32CubeF1 V1.8.6; register layouts are intentionally
 * absent. Never add tests/hal_mock to an embedded project's include paths.
 */
#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint16_t output; } GPIO_TypeDef;
typedef struct { unsigned id; } SPI_HandleTypeDef;
typedef struct { unsigned id; } ADC_HandleTypeDef;
typedef struct { unsigned id; } TIM_HandleTypeDef;
typedef struct { unsigned id; } UART_HandleTypeDef;
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
typedef struct { volatile uint32_t DEMCR; } CoreDebug_Type;
extern DWT_Type hr_mock_dwt;
extern CoreDebug_Type hr_mock_core_debug;
extern uint32_t SystemCoreClock;
#define DWT (&hr_mock_dwt)
#define CoreDebug (&hr_mock_core_debug)
#define DWT_CTRL_CYCCNTENA_Msk (1u)
#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24)

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t milliseconds);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
    const uint8_t *tx, uint8_t *rx, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef *spi,
    const uint8_t *tx, uint8_t *rx, uint16_t size);
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,
    uint32_t *data, uint32_t length);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
    const uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
    const uint8_t *data, uint16_t size);

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
void __NOP(void);

#endif
