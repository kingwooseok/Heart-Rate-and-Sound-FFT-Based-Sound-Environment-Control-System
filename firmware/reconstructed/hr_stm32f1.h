#ifndef HR_STM32F1_H
#define HR_STM32F1_H

/* Reconstructed application binding. CubeIDE owns clocks, pins and IRQ setup. */
#include "stm32f1xx_hal.h"
#include "hr_control.h"
#include "hr_sound.h"
#include "hr_lcd.h"

typedef enum { HR_ROLE_MASTER, HR_ROLE_HEART, HR_ROLE_SOUND } hr_role_t;
typedef struct { GPIO_TypeDef *port; uint16_t pin; } hr_pin_t;
typedef struct {
    hr_role_t role;
    SPI_HandleTypeDef *spi;
    ADC_HandleTypeDef *adc;                 /* Sensor nodes only. */
    TIM_HandleTypeDef *sample_timer;        /* Sound: 8 kHz ADC trigger. */
    UART_HandleTypeDef *debug_uart;         /* Optional, 115200 baud. */
    UART_HandleTypeDef *player_uart;        /* Master: 9600 baud. */
    hr_pin_t chip_select[2];                /* Master: sound, heart. */
    hr_pin_t led[3];                        /* Heart: blue, green, red. */
    hr_pin_t lcd[6];                        /* Master: RS, E, D4, D5, D6, D7; RW=GND. */
    hr_master_config_t focus;
    uint16_t track;                         /* DFPlayer repeat track, 1..2999. */
} hr_board_t;

/* Allocate one static instance per MCU, never on main()'s small stack. */
typedef struct {
    hr_board_t board;
    hr_master_t master;
    hr_sound_t sound;
    hr_spectrum_t spectrum;
    hr_lcd_t lcd;
    uint16_t adc_dma[256];                  /* Two 128-sample sound blocks. */
    uint16_t pending_samples[128];
    uint16_t work_samples[128];
    volatile uint16_t adc_latest;
    volatile uint8_t adc_ready, block_ready, adc_failed;
    volatile uint8_t published, received_count, spi_retry;
    uint8_t spi_tx, spi_rx;
    volatile uint8_t debug_busy;
    char debug_frame[512];
    uint32_t last_poll_ms, last_debug_ms, boot_ms;
    volatile uint32_t last_adc_ms;
    volatile uint32_t dropped_blocks, spi_errors;
    uint8_t player_known, player_playing;
    bool initialized;
} hr_stm32_t;

bool hr_stm32_init(hr_stm32_t *app, const hr_board_t *board);
void hr_stm32_poll(hr_stm32_t *app);
void hr_stm32_adc_half(hr_stm32_t *app, ADC_HandleTypeDef *adc);
void hr_stm32_adc_full(hr_stm32_t *app, ADC_HandleTypeDef *adc);
void hr_stm32_adc_error(hr_stm32_t *app, ADC_HandleTypeDef *adc);
void hr_stm32_spi_done(hr_stm32_t *app, SPI_HandleTypeDef *spi);
void hr_stm32_spi_error(hr_stm32_t *app, SPI_HandleTypeDef *spi);
void hr_stm32_uart_done(hr_stm32_t *app, UART_HandleTypeDef *uart);

#endif
