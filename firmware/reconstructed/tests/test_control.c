#include "hr_control.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_scaling_and_led(void)
{
    static const struct {
        uint16_t adc;
        uint8_t scaled;
    } cases[] = {
        {0u, 0u}, {48u, 1u}, {1200u, 25u}, {2399u, 49u},
        {2400u, 50u}, {2419u, 50u}, {2420u, 51u}, {2499u, 54u},
        {2500u, 60u}, {2505u, 61u}, {2699u, 99u}, {2700u, 100u},
        {2775u, 110u}, {2999u, 139u}, {3000u, 140u}, {3001u, 150u},
        {4095u, 150u}, {UINT16_MAX, 150u}
    };
    size_t i;
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert(hr_heart_scale(cases[i].adc) == cases[i].scaled);
    }
    assert(hr_heart_led(0u) == HR_LED_BLUE);
    assert(hr_heart_led(60u) == HR_LED_BLUE);
    assert(hr_heart_led(61u) == HR_LED_GREEN);
    assert(hr_heart_led(110u) == HR_LED_GREEN);
    assert(hr_heart_led(111u) == HR_LED_RED);
    assert(hr_heart_led(150u) == HR_LED_RED);
}

static void test_master_timer(void)
{
    hr_master_t master;
    assert(hr_master_init(&master, NULL, 10u));
    assert(!master.focused && !master.inputs_valid && master.seconds == 0u);
    hr_master_update(&master, 100u, 8u, 80u, true);
    assert(master.focused && master.seconds == 0u);
    hr_master_update(&master, 1099u, 8u, 80u, true);
    assert(master.seconds == 0u);
    hr_master_update(&master, 1100u, 8u, 80u, true);
    assert(master.seconds == 1u);
    hr_master_update(&master, 2651u, 63u, 110u, true);
    assert(master.seconds == 2u);
    hr_master_update(&master, 3100u, 0u, 61u, true);
    assert(master.seconds == 3u); /* Preserve subsecond remainder. */
    hr_master_update(&master, 3110u, 8u, 60u, true);
    assert(!master.focused && master.seconds == 0u);
    hr_master_update(&master, 4100u, 8u, 80u, true);
    assert(master.focused && master.seconds == 0u);
    hr_master_update(&master, 6100u, 8u, 80u, false);
    assert(!master.focused && !master.inputs_valid && master.seconds == 0u);
    hr_master_update(&master, 7100u, 64u, 80u, true);
    assert(!master.focused && !master.inputs_valid);
    hr_master_update(&master, 8100u, 8u, 151u, true);
    assert(!master.focused && !master.inputs_valid);

    assert(hr_master_init(&master, NULL, UINT32_MAX - 499u));
    hr_master_update(&master, UINT32_MAX - 499u, 8u, 80u, true);
    hr_master_update(&master, 500u, 8u, 80u, true);
    assert(master.seconds == 1u);
    hr_master_update(&master, 1500u, 8u, 80u, true);
    assert(master.seconds == 2u);
    master.seconds = UINT32_MAX - 1u;
    hr_master_update(&master, 4500u, 8u, 80u, true);
    assert(master.seconds == UINT32_MAX);
    hr_master_update(&master, 5500u, 8u, 80u, true);
    assert(master.seconds == UINT32_MAX);

    assert(hr_master_init(&master, NULL, 0u));
    hr_master_update(&master, 0u, 8u, 80u, true);
    hr_master_update(&master, 256000u, 8u, 80u, true);
    assert(master.seconds == 256u && (uint8_t)master.seconds == 0u);
}

static void test_master_policy(void)
{
    hr_master_t master;
    hr_master_config_t config = {70u, 100u, 4u, 16u};
    assert(hr_master_init(&master, &config, 0u));
    hr_master_update(&master, 10u, 3u, 80u, true);
    assert(!master.focused);
    hr_master_update(&master, 20u, 4u, 70u, true);
    assert(master.focused);
    hr_master_update(&master, 30u, 16u, 100u, true);
    assert(master.focused);
    hr_master_update(&master, 40u, 17u, 80u, true);
    assert(!master.focused);
    config.heart_max = 69u;
    assert(!hr_master_init(&master, &config, 0u));
    config.heart_max = 151u;
    assert(!hr_master_init(&master, &config, 0u));
    config.heart_max = 100u;
    config.sound_bin_max = 3u;
    assert(!hr_master_init(&master, &config, 0u));
    config.sound_bin_max = 64u;
    assert(!hr_master_init(&master, &config, 0u));
    assert(!hr_master_init(NULL, NULL, 0u));
}

static void test_dfplayer(void)
{
    static const uint8_t first_track[10] = {
        0x7Eu, 0xFFu, 0x06u, 0x03u, 0x00u, 0x00u, 0x01u, 0xFEu, 0xF7u, 0xEFu
    };
    static const uint8_t repeat_track[10] = {
        0x7Eu, 0xFFu, 0x06u, 0x08u, 0x00u, 0x00u, 0x01u, 0xFEu, 0xF2u, 0xEFu
    };
    uint8_t frame[10];
    uint16_t sum;
    unsigned i;
    hr_dfplayer_command(0x03u, 1u, frame);
    assert(memcmp(frame, first_track, sizeof(frame)) == 0);
    hr_dfplayer_command(0x08u, 1u, frame);
    /* Checksum is -(FF+06+08+00+00+01) = FEF2. */
    assert(memcmp(frame, repeat_track, sizeof(frame)) == 0);
    hr_dfplayer_command(0x16u, 0u, frame);
    assert(frame[3] == 0x16u && frame[7] == 0xFEu && frame[8] == 0xE5u);
    hr_dfplayer_command(0x0Fu, 0x0B64u, frame);
    assert(frame[5] == 0x0Bu && frame[6] == 0x64u);
    sum = (uint16_t)(((uint16_t)frame[7] << 8) | frame[8]);
    for (i = 1u; i <= 6u; ++i) {
        sum = (uint16_t)(sum + frame[i]);
    }
    assert(sum == 0u);
}

static void test_lcd(void)
{
    struct {
        uint8_t before;
        char lines[2][17];
        uint8_t after;
    } buffer = {0xA5u, {{0}, {0}}, 0x5Au};
    hr_master_t master;
    assert(hr_master_init(&master, NULL, 0u));
    hr_master_lcd(&master, buffer.lines);
    assert(strcmp(buffer.lines[0], "HR:--- BIN:--   ") == 0);
    assert(strcmp(buffer.lines[1], "I CNT:0         ") == 0);
    hr_master_update(&master, 0u, 63u, 110u, true);
    master.seconds = UINT32_MAX;
    hr_master_lcd(&master, buffer.lines);
    assert(strcmp(buffer.lines[0], "HR:110 BIN:63   ") == 0);
    assert(strcmp(buffer.lines[1], "F CNT:4294967295") == 0);
    hr_master_update(&master, 1u, 8u, 0u, true);
    hr_master_lcd(&master, buffer.lines);
    assert(strcmp(buffer.lines[0], "HR:0   BIN:8    ") == 0);
    assert(strcmp(buffer.lines[1], "I CNT:0         ") == 0);
    assert(buffer.before == 0xA5u && buffer.after == 0x5Au);
}

int test_control(void)
{
    test_scaling_and_led();
    test_master_timer();
    test_master_policy();
    test_dfplayer();
    test_lcd();
    return 0;
}
