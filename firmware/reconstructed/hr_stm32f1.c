#include "hr_stm32f1.h"
#include <stdio.h>
#include <string.h>

static bool pin_valid(hr_pin_t pin) {
    return pin.port != NULL && pin.pin != 0 && (pin.pin & (pin.pin - 1u)) == 0;
}
static void pin_write(hr_pin_t pin, bool high) {
    HAL_GPIO_WritePin(pin.port, pin.pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static void lcd_pins(void *context, bool rs, uint8_t nibble, bool enable) {
    hr_stm32_t *app = (hr_stm32_t *)context;
    unsigned i;
    pin_write(app->board.lcd[0], rs);
    for (i = 0; i < 4; ++i) pin_write(app->board.lcd[i + 2], (nibble & (1u << i)) != 0);
    pin_write(app->board.lcd[1], enable);
}
static void lcd_delay(void *context, uint32_t us) {
    uint32_t start, cycles;
    (void)context;
    if (us >= 1000u) {
        HAL_Delay((us + 999u) / 1000u);
        return;
    }
    /* Cortex-M3 cycle counter; leave its running value intact for profiling. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cycles = (uint32_t)(((uint64_t)SystemCoreClock * us + 999999u) / 1000000u);
    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) { __NOP(); }
}
static void arm_slave(hr_stm32_t *app) {
    /* Keep this byte stable until completion: SPI must be armed before SCK. */
    app->spi_tx = app->adc_failed ? 0xff : app->published;
    app->spi_retry = 0;
    if (HAL_SPI_TransmitReceive_IT(app->board.spi, &app->spi_tx, &app->spi_rx, 1) != HAL_OK) {
        app->spi_retry = 1;
        ++app->spi_errors;
    }
}

bool hr_stm32_init(hr_stm32_t *app, const hr_board_t *board) {
    unsigned i;
    hr_lcd_io_t lcd_io;
    if (app == NULL || board == NULL || board->spi == NULL) return false;
    if (board->role != HR_ROLE_MASTER && board->role != HR_ROLE_HEART && board->role != HR_ROLE_SOUND)
        return false;
    if (board->role == HR_ROLE_MASTER) {
        if (board->player_uart == NULL || board->player_uart == board->debug_uart ||
            board->track == 0 || board->track > 2999) return false;
        for (i = 0; i < 2; ++i) if (!pin_valid(board->chip_select[i])) return false;
        for (i = 0; i < 6; ++i) if (!pin_valid(board->lcd[i])) return false;
    } else {
        if (board->adc == NULL) return false;
        if (board->role == HR_ROLE_SOUND && board->sample_timer == NULL) return false;
        if (board->role == HR_ROLE_HEART)
            for (i = 0; i < 3; ++i) if (!pin_valid(board->led[i])) return false;
    }
    memset(app, 0, sizeof(*app));
    app->board = *board;
    app->published = 0xff; /* Reserved unavailable value until first ADC block. */
    app->boot_ms = HAL_GetTick();
    app->last_adc_ms = app->boot_ms;
    app->last_poll_ms = app->boot_ms;
    app->last_debug_ms = app->boot_ms;
    if (board->role == HR_ROLE_MASTER) {
        if (!hr_master_init(&app->master, &board->focus, app->boot_ms)) return false;
        pin_write(board->chip_select[0], true);
        pin_write(board->chip_select[1], true);
        lcd_io.ctx = app;
        lcd_io.write_pins = lcd_pins;
        lcd_io.delay_us = lcd_delay;
        if (!hr_lcd_init(&app->lcd, lcd_io)) return false;
    } else {
        if (board->role == HR_ROLE_SOUND) hr_sound_init(&app->sound);
        if (board->role == HR_ROLE_HEART)
            for (i = 0; i < 3; ++i) pin_write(board->led[i], false);
        if (HAL_ADCEx_Calibration_Start(board->adc) != HAL_OK) return false;
        app->initialized = true; /* DMA IRQs can start during the following calls. */
        if (HAL_ADC_Start_DMA(board->adc, (uint32_t *)(void *)app->adc_dma,
                              board->role == HR_ROLE_SOUND ? 256u : 32u) != HAL_OK) {
            app->initialized = false;
            return false;
        }
        if (board->sample_timer != NULL && HAL_TIM_Base_Start(board->sample_timer) != HAL_OK) {
            (void)HAL_ADC_Stop_DMA(board->adc);
            app->initialized = false;
            return false;
        }
        arm_slave(app);
    }
    app->initialized = true;
    return true;
}

static void adc_block(hr_stm32_t *app, ADC_HandleTypeDef *adc, unsigned half) {
    if (app == NULL || !app->initialized || app->adc_failed ||
        adc != app->board.adc || app->board.role == HR_ROLE_MASTER)
        return;
    app->last_adc_ms = HAL_GetTick();
    if (app->board.role == HR_ROLE_HEART) {
        app->adc_latest = app->adc_dma[half * 16u + 15u];
        app->adc_ready = 1;
    } else if (!app->block_ready) {
        memcpy(app->pending_samples, &app->adc_dma[half * 128u], sizeof(app->pending_samples));
        __DMB();
        app->block_ready = 1;
    } else {
        ++app->dropped_blocks; /* Never mix samples from different DMA halves. */
    }
}
void hr_stm32_adc_half(hr_stm32_t *app, ADC_HandleTypeDef *adc) { adc_block(app, adc, 0); }
void hr_stm32_adc_full(hr_stm32_t *app, ADC_HandleTypeDef *adc) { adc_block(app, adc, 1); }
void hr_stm32_adc_error(hr_stm32_t *app, ADC_HandleTypeDef *adc) {
    if (app != NULL && app->initialized && adc == app->board.adc && app->board.role != HR_ROLE_MASTER) {
        app->adc_failed = 1;
        app->published = 0xff;
    }
}
void hr_stm32_spi_done(hr_stm32_t *app, SPI_HandleTypeDef *spi) {
    if (app != NULL && app->initialized && spi == app->board.spi && app->board.role != HR_ROLE_MASTER) {
        app->received_count = app->spi_rx;
        arm_slave(app);
    }
}
void hr_stm32_spi_error(hr_stm32_t *app, SPI_HandleTypeDef *spi) {
    if (app != NULL && app->initialized && spi == app->board.spi && app->board.role != HR_ROLE_MASTER) {
        app->spi_retry = 1; /* Recover in main(), not inside the IRQ callback. */
        ++app->spi_errors;
    }
}
void hr_stm32_uart_done(hr_stm32_t *app, UART_HandleTypeDef *uart) {
    if (app != NULL && uart == app->board.debug_uart) app->debug_busy = 0;
}
static void debug_send(hr_stm32_t *app, size_t length) {
    if (length == 0 || length >= sizeof(app->debug_frame)) return;
    app->debug_busy = 1;
    if (HAL_UART_Transmit_IT(app->board.debug_uart, (uint8_t *)app->debug_frame, (uint16_t)length) != HAL_OK)
        app->debug_busy = 0;
}
static bool exchange(hr_stm32_t *app, unsigned node, uint8_t count, uint8_t *value) {
    HAL_StatusTypeDef status;
    pin_write(app->board.chip_select[node], false);
    status = HAL_SPI_TransmitReceive(app->board.spi, &count, value, 1, 10);
    pin_write(app->board.chip_select[node], true);
    return status == HAL_OK;
}
void hr_stm32_poll(hr_stm32_t *app) {
    uint32_t now, saved_mask, adc_tick;
    uint16_t adc = 0;
    unsigned i;
    if (app == NULL || !app->initialized) return;
    now = HAL_GetTick();
    if (app->board.role == HR_ROLE_MASTER) {
        uint8_t sound = 0xff, heart = 0xff, command[10];
        bool sound_ok, heart_ok;
        char lines[2][17];
        if ((uint32_t)(now - app->last_poll_ms) < 200u) return;
        app->last_poll_ms = now;
        sound_ok = exchange(app, 0, (uint8_t)app->master.seconds, &sound);
        heart_ok = exchange(app, 1, (uint8_t)app->master.seconds, &heart);
        hr_master_update(&app->master, now, sound, heart, sound_ok && heart_ok);
        hr_master_lcd(&app->master, lines);
        hr_lcd_write_lines(&app->lcd, (const char (*)[17])lines);
        /* Allow the MP3 module to boot, then send only transitions/retries. */
        if ((uint32_t)(now - app->boot_ms) >= 2000u &&
            (!app->player_known || app->player_playing != (uint8_t)app->master.focused)) {
            hr_dfplayer_command(app->master.focused ? 0x08 : 0x16,
                                app->master.focused ? app->board.track : 0, command);
            if (HAL_UART_Transmit(app->board.player_uart, command, sizeof(command), 50) == HAL_OK) {
                app->player_known = 1;
                app->player_playing = (uint8_t)app->master.focused;
            }
        }
        return;
    }
    if (app->spi_retry) {
        if (HAL_SPI_Abort(app->board.spi) == HAL_OK) arm_slave(app);
    }
    if (app->board.role == HR_ROLE_HEART && app->adc_ready) {
        saved_mask = __get_PRIMASK();
        __disable_irq();
        adc = app->adc_latest;
        app->adc_ready = 0;
        __set_PRIMASK(saved_mask);
        app->published = adc <= 4095 ? hr_heart_scale(adc) : 0xff;
        for (i = 0; i < 3; ++i)
            pin_write(app->board.led[i], app->published != 0xff && (unsigned)hr_heart_led(app->published) == i);
    }
    if (app->board.role == HR_ROLE_SOUND && app->block_ready) {
        saved_mask = __get_PRIMASK();
        __disable_irq();
        memcpy(app->work_samples, app->pending_samples, sizeof(app->work_samples));
        app->block_ready = 0;
        __set_PRIMASK(saved_mask);
        if (hr_sound_analyze(&app->sound, app->work_samples, &app->spectrum))
            app->published = app->spectrum.peak_bin;
        else app->published = 0xff;
    }
    /* Read the event timestamp before the current tick: an ADC IRQ during FFT
     * must not make an old 'now' underflow and falsely expire fresh data. */
    adc_tick = app->last_adc_ms;
    now = HAL_GetTick();
    if (app->adc_failed || (uint32_t)(now - adc_tick) >= 1000u) {
        app->published = 0xff;
        if (app->board.role == HR_ROLE_HEART)
            for (i = 0; i < 3; ++i) pin_write(app->board.led[i], false);
    }
    if (app->board.debug_uart != NULL && !app->debug_busy && app->published != 0xff &&
        (uint32_t)(now - app->last_debug_ms) >= 200u) {
        app->last_debug_ms = now;
        if (app->board.role == HR_ROLE_HEART) {
            int length = snprintf(app->debug_frame, sizeof(app->debug_frame), "ADC=%u   HR=%u CNT=%u\r\n",
                                  (unsigned)app->adc_latest, (unsigned)app->published,
                                  (unsigned)app->received_count);
            if (length > 0) debug_send(app, (size_t)length);
        } else {
            debug_send(app, hr_sound_format_fft(&app->spectrum, app->debug_frame, sizeof(app->debug_frame)));
        }
    }
}
