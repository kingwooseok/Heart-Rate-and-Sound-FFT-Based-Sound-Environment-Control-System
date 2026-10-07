#include "hr_stm32f1.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

/* Three application instances share a deterministic host SPI wire model.
 * These tests exercise application calls/callbacks, not STM32 peripheral RTL.
 */
enum { MASTER = 0, HEART = 1, SOUND = 2, PLAYER = 3 };
typedef struct {
    const uint8_t *tx;
    uint8_t *rx;
    bool active;
    HAL_StatusTypeDef next_arm;
    uint32_t *dma_data;
    uint32_t dma_length;
    bool dma_active;
    unsigned aborts;
    unsigned arms;
} mock_node_t;
typedef struct {
    const uint8_t *data;
    uint8_t snapshot[512];
    uint16_t length;
    unsigned calls;
    bool active;
} mock_uart_t;
static hr_stm32_t apps[3];
static hr_board_t boards[3];
static SPI_HandleTypeDef spis[3];
static ADC_HandleTypeDef adcs[3];
static UART_HandleTypeDef uarts[4];
static TIM_HandleTypeDef sample_timer;
static GPIO_TypeDef ports[3];
static mock_node_t nodes[3];
static mock_uart_t serial[4];
static uint32_t tick_ms, irq_mask;
static bool inject_adc_on_restore;
static unsigned dma_stops, calibrations, timer_starts, exchanges;
static HAL_StatusTypeDef calibration_result, dma_result, timer_result;
static HAL_StatusTypeDef transfer_result[2], player_result, debug_result;
static uint8_t last_counter[2], last_value[2], player_frame[10];
DWT_Type hr_mock_dwt;
CoreDebug_Type hr_mock_core_debug;
uint32_t SystemCoreClock = 72000000u;

uint32_t HAL_GetTick(void) { return tick_ms; }
void HAL_Delay(uint32_t milliseconds) { tick_ms += milliseconds; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1u; }
void __set_PRIMASK(uint32_t value)
{
    irq_mask = value;
    if (value == 0u && inject_adc_on_restore) {
        inject_adc_on_restore = false;
        ++tick_ms;
        hr_stm32_adc_half(&apps[SOUND], &adcs[SOUND]);
    }
}
void __DMB(void) { }
void __NOP(void) { hr_mock_dwt.CYCCNT += 72u; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    assert(port != NULL && pin != 0u);
    if (state == GPIO_PIN_SET) port->output |= pin;
    else port->output &= (uint16_t)~pin;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive_IT(SPI_HandleTypeDef *spi,
    const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    mock_node_t *node = &nodes[spi->id];
    HAL_StatusTypeDef result = node->next_arm;
    assert(size == 1u && spi->id != MASTER);
    ++node->arms;
    node->next_arm = HAL_OK;
    if (result != HAL_OK) return result;
    assert(!node->active);
    node->tx = tx;
    node->rx = rx;
    node->active = true;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
    const uint8_t *tx, uint8_t *rx, uint16_t size, uint32_t timeout)
{
    unsigned selected, receiver;
    mock_node_t *node;
    assert(spi->id == MASTER && size == 1u && timeout > 0u);
    assert((ports[0].output & 3u) == 1u || (ports[0].output & 3u) == 2u);
    selected = (ports[0].output & 1u) == 0u ? 0u : 1u;
    receiver = selected == 0u ? SOUND : HEART;
    ++exchanges;
    last_counter[selected] = *tx;
    if (transfer_result[selected] != HAL_OK) return transfer_result[selected];
    node = &nodes[receiver];
    if (!node->active) return HAL_TIMEOUT;
    *rx = *node->tx;
    last_value[selected] = *rx;
    *node->rx = *tx;
    node->active = false;
    hr_stm32_spi_done(&apps[receiver], &spis[receiver]);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *spi)
{
    ++nodes[spi->id].aborts;
    nodes[spi->id].active = false;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc)
{
    assert(adc->id != MASTER);
    ++calibrations;
    return calibration_result;
}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,
    uint32_t *data, uint32_t length)
{
    if (dma_result != HAL_OK) return dma_result;
    nodes[adc->id].dma_data = data;
    nodes[adc->id].dma_length = length;
    nodes[adc->id].dma_active = true;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc)
{
    ++dma_stops;
    nodes[adc->id].dma_active = false;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{
    assert(timer == &sample_timer);
    ++timer_starts;
    return timer_result;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
    const uint8_t *data, uint16_t size, uint32_t timeout)
{
    assert(uart->id == PLAYER && size == 10u && timeout > 0u);
    ++serial[PLAYER].calls;
    memcpy(player_frame, data, sizeof(player_frame));
    return player_result;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart,
    const uint8_t *data, uint16_t size)
{
    mock_uart_t *output = &serial[uart->id];
    assert(uart->id != PLAYER && size < sizeof(output->snapshot));
    ++output->calls;
    if (debug_result != HAL_OK) return debug_result;
    assert(!output->active);
    output->active = true;
    output->data = data;
    output->length = size;
    memcpy(output->snapshot, data, size);
    return HAL_OK;
}

static void reset_mock(void)
{
    unsigned i;
    const hr_master_config_t defaults = HR_MASTER_DEFAULT_CONFIG;
    memset(apps, 0, sizeof(apps));
    memset(boards, 0, sizeof(boards));
    memset(nodes, 0, sizeof(nodes));
    memset(serial, 0, sizeof(serial));
    memset(ports, 0, sizeof(ports));
    tick_ms = irq_mask = 0u;
    inject_adc_on_restore = false;
    dma_stops = calibrations = timer_starts = exchanges = 0u;
    calibration_result = dma_result = timer_result = HAL_OK;
    transfer_result[0] = transfer_result[1] = HAL_OK;
    player_result = debug_result = HAL_OK;
    memset(last_counter, 0, sizeof(last_counter));
    memset(last_value, 0, sizeof(last_value));
    memset(player_frame, 0, sizeof(player_frame));
    for (i = 0u; i < 3u; ++i) {
        spis[i].id = adcs[i].id = uarts[i].id = i;
        boards[i].spi = &spis[i];
        boards[i].adc = &adcs[i];
        boards[i].debug_uart = &uarts[i];
        boards[i].role = (hr_role_t)i;
    }
    uarts[PLAYER].id = PLAYER;
    boards[MASTER].player_uart = &uarts[PLAYER];
    boards[MASTER].track = 1u;
    boards[MASTER].focus = defaults;
    for (i = 0u; i < 2u; ++i) {
        boards[MASTER].chip_select[i].port = &ports[0];
        boards[MASTER].chip_select[i].pin = (uint16_t)(1u << i);
    }
    for (i = 0u; i < 6u; ++i) {
        boards[MASTER].lcd[i].port = &ports[1];
        boards[MASTER].lcd[i].pin = (uint16_t)(1u << i);
    }
    for (i = 0u; i < 3u; ++i) {
        boards[HEART].led[i].port = &ports[2];
        boards[HEART].led[i].pin = (uint16_t)(1u << i);
    }
    boards[SOUND].sample_timer = &sample_timer;
}
static void complete_debug(unsigned role)
{
    mock_uart_t *output = &serial[role];
    assert(output->active);
    assert(memcmp(output->snapshot, output->data, output->length) == 0);
    output->active = false;
    hr_stm32_uart_done(&apps[role], &uarts[role]);
}
static void heart_sample(uint16_t value)
{
    apps[HEART].adc_dma[15] = value;
    hr_stm32_adc_half(&apps[HEART], &adcs[HEART]);
    hr_stm32_poll(&apps[HEART]);
}
static void sound_samples(unsigned bin)
{
    unsigned i;
    for (i = 0u; i < 128u; ++i) {
        apps[SOUND].adc_dma[i] = (uint16_t)(2048.0 + 500.0 *
            sin(6.283185307179586 * (double)bin * (double)i / 128.0));
    }
    hr_stm32_adc_half(&apps[SOUND], &adcs[SOUND]);
    hr_stm32_poll(&apps[SOUND]);
}
static void master_poll_after(uint32_t ms)
{
    tick_ms += ms;
    hr_stm32_poll(&apps[MASTER]);
    assert((ports[0].output & 3u) == 3u); /* Both CS lines released. */
}

static void test_three_node_path(void)
{
    uint32_t focus_tick;
    unsigned player_calls, prior_exchanges;
    reset_mock();
    assert(hr_stm32_init(&apps[HEART], &boards[HEART]));
    assert(hr_stm32_init(&apps[SOUND], &boards[SOUND]));
    assert(hr_stm32_init(&apps[MASTER], &boards[MASTER]));
    assert(calibrations == 2u && timer_starts == 1u);
    assert(nodes[HEART].dma_length == 32u && nodes[SOUND].dma_length == 256u);
    assert(nodes[HEART].dma_data == (uint32_t *)(void *)apps[HEART].adc_dma);
    assert(nodes[SOUND].dma_data == (uint32_t *)(void *)apps[SOUND].adc_dma);
    master_poll_after(200u);
    assert(last_value[0] == 0xffu && last_value[1] == 0xffu);
    assert(!apps[MASTER].master.focused);
    heart_sample(2600u);
    sound_samples(16u);
    assert(apps[HEART].published == 80u && apps[SOUND].published == 16u);
    assert((ports[2].output & 7u) == 2u);
    /* The byte already armed before this ADC update stays stable until clocked. */
    master_poll_after(200u);
    assert(last_value[0] == 0xffu && last_value[1] == 0xffu);
    focus_tick = tick_ms + 200u;
    master_poll_after(200u);
    assert(last_value[0] == 16u && last_value[1] == 80u);
    assert(apps[MASTER].master.focused && apps[MASTER].master.seconds == 0u);
    tick_ms = focus_tick + 2500u;
    player_result = HAL_TIMEOUT;
    hr_stm32_poll(&apps[MASTER]);
    assert(apps[MASTER].master.seconds == 2u);
    assert(!apps[MASTER].player_known && serial[PLAYER].calls == 1u);
    assert(player_frame[3] == 0x08u && player_frame[6] == 1u);
    player_result = HAL_OK;
    master_poll_after(200u);
    assert(apps[MASTER].player_known && apps[MASTER].player_playing);
    assert(last_counter[0] == 2u && last_counter[1] == 2u);
    assert(apps[HEART].received_count == 2u && apps[SOUND].received_count == 2u);
    player_calls = serial[PLAYER].calls;
    master_poll_after(200u);
    assert(serial[PLAYER].calls == player_calls); /* No repeated play command. */
    transfer_result[0] = HAL_ERROR;
    prior_exchanges = exchanges;
    master_poll_after(200u);
    assert(!apps[MASTER].master.focused && apps[MASTER].master.seconds == 0u);
    assert(!apps[MASTER].player_playing && player_frame[3] == 0x16u);
    assert(exchanges == prior_exchanges + 2u); /* Heart also polled after a sound error. */
}

static void test_heart_debug_and_recovery(void)
{
    unsigned prior_arms, prior_calls;
    reset_mock();
    nodes[HEART].next_arm = HAL_BUSY;
    assert(hr_stm32_init(&apps[HEART], &boards[HEART]));
    assert(apps[HEART].spi_retry && apps[HEART].spi_errors == 1u);
    hr_stm32_poll(&apps[HEART]);
    assert(!apps[HEART].spi_retry && nodes[HEART].active && nodes[HEART].aborts == 1u);
    prior_arms = nodes[HEART].arms;
    hr_stm32_spi_error(&apps[HEART], &spis[SOUND]);
    assert(!apps[HEART].spi_retry);
    hr_stm32_spi_error(&apps[HEART], &spis[HEART]);
    hr_stm32_poll(&apps[HEART]);
    assert(nodes[HEART].arms == prior_arms + 1u && nodes[HEART].aborts == 2u);

    tick_ms = 200u;
    irq_mask = 1u; /* Critical section must restore a previously masked state. */
    heart_sample(2400u);
    assert(irq_mask == 1u && apps[HEART].published == 50u);
    irq_mask = 0u;
    assert((ports[2].output & 7u) == 1u && apps[HEART].debug_busy);
    assert(serial[HEART].active && strstr(apps[HEART].debug_frame, "ADC=2400") != NULL);
    prior_calls = serial[HEART].calls;
    tick_ms += 200u;
    heart_sample(3001u);
    assert((ports[2].output & 7u) == 4u);
    assert(serial[HEART].calls == prior_calls); /* An active TX owns its buffer. */
    complete_debug(HEART);
    assert(!apps[HEART].debug_busy);
    hr_stm32_poll(&apps[HEART]);
    assert(serial[HEART].calls == prior_calls + 1u);
    assert(strstr(apps[HEART].debug_frame, "ADC=3001") != NULL);
    complete_debug(HEART);
    debug_result = HAL_BUSY;
    tick_ms += 200u;
    hr_stm32_poll(&apps[HEART]);
    assert(!apps[HEART].debug_busy && !serial[HEART].active);
    heart_sample(5000u);
    assert(apps[HEART].published == 0xffu && (ports[2].output & 7u) == 0u);
    apps[HEART].adc_dma[31] = 2505u;
    hr_stm32_adc_full(&apps[HEART], &adcs[HEART]);
    hr_stm32_poll(&apps[HEART]);
    assert(apps[HEART].published == 61u && (ports[2].output & 7u) == 2u);
}

static void test_sound_queue(void)
{
    unsigned i;
    reset_mock();
    assert(hr_stm32_init(&apps[SOUND], &boards[SOUND]));
    for (i = 0u; i < 128u; ++i) {
        apps[SOUND].adc_dma[i] = 1000u;
        apps[SOUND].adc_dma[i + 128u] = 3000u;
    }
    hr_stm32_adc_half(&apps[SOUND], &adcs[HEART]);
    assert(!apps[SOUND].block_ready);
    hr_stm32_adc_half(&apps[SOUND], &adcs[SOUND]);
    hr_stm32_adc_full(&apps[SOUND], &adcs[SOUND]);
    assert(apps[SOUND].block_ready && apps[SOUND].dropped_blocks == 1u);
    for (i = 0u; i < 128u; ++i) assert(apps[SOUND].pending_samples[i] == 1000u);
    hr_stm32_poll(&apps[SOUND]);
    assert(!apps[SOUND].block_ready && apps[SOUND].published == 0u);
    assert(irq_mask == 0u);
    for (i = 0u; i < 128u; ++i) assert(apps[SOUND].work_samples[i] == 1000u);
    hr_stm32_adc_full(&apps[SOUND], &adcs[SOUND]);
    assert(apps[SOUND].pending_samples[0] == 3000u);
    hr_stm32_poll(&apps[SOUND]);
    assert(apps[SOUND].work_samples[127] == 3000u);
    tick_ms = 200u;
    sound_samples(8u);
    assert(apps[SOUND].published == 8u && serial[SOUND].active);
    assert(memcmp(serial[SOUND].snapshot, "FFT:", 4u) == 0);
    tick_ms += 200u;
    sound_samples(20u);
    assert(apps[SOUND].published == 20u);
    assert(serial[SOUND].calls == 1u);
    complete_debug(SOUND);
    apps[SOUND].adc_dma[0] = 4096u;
    hr_stm32_adc_half(&apps[SOUND], &adcs[SOUND]);
    hr_stm32_poll(&apps[SOUND]);
    assert(apps[SOUND].published == 0xffu);
}

static void test_init_failures(void)
{
    reset_mock();
    assert(!hr_stm32_init(NULL, &boards[HEART]));
    assert(!hr_stm32_init(&apps[HEART], NULL));
    calibration_result = HAL_ERROR;
    assert(!hr_stm32_init(&apps[HEART], &boards[HEART]));
    assert(!apps[HEART].initialized && !nodes[HEART].dma_active);
    calibration_result = HAL_OK;
    dma_result = HAL_ERROR;
    assert(!hr_stm32_init(&apps[HEART], &boards[HEART]));
    assert(!apps[HEART].initialized);
    dma_result = HAL_OK;
    timer_result = HAL_ERROR;
    assert(!hr_stm32_init(&apps[SOUND], &boards[SOUND]));
    assert(!apps[SOUND].initialized && !nodes[SOUND].dma_active && dma_stops == 1u);
    boards[MASTER].player_uart = boards[MASTER].debug_uart;
    assert(!hr_stm32_init(&apps[MASTER], &boards[MASTER]));
    boards[HEART].led[0].pin = 3u;
    assert(!hr_stm32_init(&apps[HEART], &boards[HEART]));
    hr_stm32_poll(NULL);
    hr_stm32_poll(&apps[MASTER]);
}

static void test_adc_timeout_and_error(void)
{
    uint32_t last_adc;
    reset_mock();
    assert(hr_stm32_init(&apps[HEART], &boards[HEART]));
    tick_ms = 500u;
    heart_sample(2600u);
    tick_ms = 1499u;
    hr_stm32_poll(&apps[HEART]);
    assert(apps[HEART].published == 80u);
    tick_ms = 1500u;
    hr_stm32_poll(&apps[HEART]);
    assert(apps[HEART].published == 0xffu && (ports[2].output & 7u) == 0u);
    tick_ms = 1510u;
    heart_sample(2600u);
    assert(apps[HEART].published == 80u);
    hr_stm32_adc_error(&apps[HEART], &adcs[SOUND]);
    assert(!apps[HEART].adc_failed);
    hr_stm32_adc_error(&apps[HEART], &adcs[HEART]);
    last_adc = apps[HEART].last_adc_ms;
    assert(apps[HEART].published == 0xffu && apps[HEART].adc_failed);
    tick_ms += 10u;
    heart_sample(2505u);
    assert(apps[HEART].published == 0xffu && (ports[2].output & 7u) == 0u);
    assert(apps[HEART].last_adc_ms == last_adc); /* Fault is latched until reinit. */

    reset_mock();
    tick_ms = UINT32_MAX - 499u;
    assert(hr_stm32_init(&apps[HEART], &boards[HEART]));
    heart_sample(2600u);
    tick_ms = 499u;
    hr_stm32_poll(&apps[HEART]);
    assert(apps[HEART].published == 80u);
    tick_ms = 500u;
    hr_stm32_poll(&apps[HEART]);
    assert(apps[HEART].published == 0xffu);

    reset_mock();
    assert(hr_stm32_init(&apps[SOUND], &boards[SOUND]));
    tick_ms = 100u;
    inject_adc_on_restore = true;
    sound_samples(8u);
    /* DMA may deliver a newer timestamp between poll entry and FFT completion. */
    assert(apps[SOUND].last_adc_ms == 101u);
    assert(apps[SOUND].published == 8u && apps[SOUND].block_ready);
    hr_stm32_adc_error(&apps[SOUND], &adcs[SOUND]);
    hr_stm32_poll(&apps[SOUND]);
    assert(apps[SOUND].published == 0xffu);
}

int test_stm32(void)
{
    test_three_node_path();
    test_heart_debug_and_recovery();
    test_sound_queue();
    test_init_failures();
    test_adc_timeout_and_error();
    return 0;
}
