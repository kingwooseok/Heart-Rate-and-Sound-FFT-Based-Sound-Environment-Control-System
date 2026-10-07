#ifndef HR_CONTROL_H
#define HR_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/* Reconstructed application behavior; no CubeIDE-generated configuration. */
typedef enum {
    HR_LED_BLUE = 0,
    HR_LED_GREEN,
    HR_LED_RED
} hr_led_t;

typedef struct {
    uint8_t heart_min;
    uint8_t heart_max;
    uint8_t sound_bin_min;
    uint8_t sound_bin_max;
} hr_master_config_t;

/* Reconstruction policy: green heart-state range, all sound bins allowed. */
#define HR_MASTER_DEFAULT_CONFIG {61u, 110u, 0u, 63u}

typedef struct {
    hr_master_config_t config;
    bool focused;
    bool inputs_valid;
    uint32_t seconds;
    uint32_t count_tick_ms;
    uint8_t heart;
    uint8_t sound_bin;
} hr_master_t;

/* Exact integer mapping retained from the surviving Slave 2 source. */
uint8_t hr_heart_scale(uint16_t adc);
hr_led_t hr_heart_led(uint8_t scaled);

/* NULL config selects HR_MASTER_DEFAULT_CONFIG. Returns false on bad ranges. */
bool hr_master_init(hr_master_t *master, const hr_master_config_t *config,
                    uint32_t now_ms);
/* valid=false means missing/expired sensor data: stop focus and reset count.
 * Call at least once per uint32_t millisecond clock cycle (about 49 days).
 * The SPI counter byte is (uint8_t)master->seconds.
 */
void hr_master_update(hr_master_t *master, uint32_t now_ms, uint8_t sound_bin,
                      uint8_t heart, bool valid);

/* DFPlayer Mini frame, feedback disabled, two's-complement 16-bit checksum. */
void hr_dfplayer_command(uint8_t command, uint16_t parameter, uint8_t out[10]);

/* Two padded 16-character lines plus NUL. HR is an amplitude index, not BPM.
 * F/I on row two denote focused/idle. Missing data displays as dashes.
 */
void hr_master_lcd(const hr_master_t *master, char lines[2][17]);

#endif
