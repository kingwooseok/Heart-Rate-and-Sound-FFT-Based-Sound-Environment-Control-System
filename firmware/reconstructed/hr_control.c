#include "hr_control.h"

#include <stddef.h>
#include <string.h>

/* Reconstructed application based on the documented behavior. */
uint8_t hr_heart_scale(uint16_t adc)
{
    uint32_t value = adc;
    if (value < 2400u) {
        return (uint8_t)((value * 50u) / 2400u);
    }
    if (value < 2500u) {
        return (uint8_t)(50u + (value - 2400u) / 20u);
    }
    if (value < 2700u) {
        return (uint8_t)(60u + ((value - 2500u) * 40u) / 200u);
    }
    if (value <= 3000u) {
        return (uint8_t)(100u + ((value - 2700u) * 40u) / 300u);
    }
    return 150u;
}

hr_led_t hr_heart_led(uint8_t scaled)
{
    if (scaled <= 60u) {
        return HR_LED_BLUE;
    }
    return scaled <= 110u ? HR_LED_GREEN : HR_LED_RED;
}

bool hr_master_init(hr_master_t *master, const hr_master_config_t *config,
                    uint32_t now_ms)
{
    const hr_master_config_t defaults = HR_MASTER_DEFAULT_CONFIG;
    if (master == NULL) {
        return false;
    }
    memset(master, 0, sizeof(*master));
    if (config == NULL) {
        config = &defaults;
    }
    if (config->heart_min > config->heart_max || config->heart_max > 150u ||
        config->sound_bin_min > config->sound_bin_max ||
        config->sound_bin_max > 63u) {
        return false;
    }
    master->config = *config;
    master->count_tick_ms = now_ms;
    return true;
}

void hr_master_update(hr_master_t *master, uint32_t now_ms, uint8_t sound_bin,
                      uint8_t heart, bool valid)
{
    bool focus;
    uint32_t elapsed_seconds;
    if (master == NULL) {
        return;
    }
    master->heart = heart;
    master->sound_bin = sound_bin;
    master->inputs_valid = valid && heart <= 150u && sound_bin <= 63u;
    focus = master->inputs_valid && heart >= master->config.heart_min &&
            heart <= master->config.heart_max &&
            sound_bin >= master->config.sound_bin_min &&
            sound_bin <= master->config.sound_bin_max;
    if (!focus || !master->focused) {
        master->seconds = 0u;
        master->count_tick_ms = now_ms;
    } else {
        /* Unsigned subtraction also covers the HAL_GetTick() wrap. */
        elapsed_seconds = (uint32_t)(now_ms - master->count_tick_ms) / 1000u;
        master->count_tick_ms += elapsed_seconds * 1000u;
        if (elapsed_seconds > UINT32_MAX - master->seconds) {
            master->seconds = UINT32_MAX;
        } else {
            master->seconds += elapsed_seconds;
        }
    }
    master->focused = focus;
}

void hr_dfplayer_command(uint8_t command, uint16_t parameter, uint8_t out[10])
{
    uint16_t checksum;
    if (out == NULL) {
        return;
    }
    out[0] = 0x7Eu;
    out[1] = 0xFFu;
    out[2] = 0x06u;
    out[3] = command;
    out[4] = 0u;
    out[5] = (uint8_t)(parameter >> 8);
    out[6] = (uint8_t)parameter;
    checksum = (uint16_t)(0u - (uint16_t)(out[1] + out[2] + out[3] +
                                          out[4] + out[5] + out[6]));
    out[7] = (uint8_t)(checksum >> 8);
    out[8] = (uint8_t)checksum;
    out[9] = 0xEFu;
}

static void write_decimal(char *destination, uint32_t value)
{
    char reversed[10];
    unsigned digits = 0u;
    do {
        reversed[digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (digits > 0u) {
        *destination++ = reversed[--digits];
    }
}

void hr_master_lcd(const hr_master_t *master, char lines[2][17])
{
    unsigned row;
    if (master == NULL || lines == NULL) {
        return;
    }
    for (row = 0u; row < 2u; ++row) {
        memset(lines[row], ' ', 16u);
        lines[row][16] = '\0';
    }
    memcpy(lines[0], "HR:--- BIN:--", 13u);
    if (master->inputs_valid) {
        memset(lines[0] + 3, ' ', 3u);
        memset(lines[0] + 11, ' ', 2u);
        write_decimal(lines[0] + 3, master->heart);
        write_decimal(lines[0] + 11, master->sound_bin);
    }
    lines[1][0] = master->focused ? 'F' : 'I';
    memcpy(lines[1] + 1, " CNT:", 5u);
    write_decimal(lines[1] + 6, master->seconds);
}
