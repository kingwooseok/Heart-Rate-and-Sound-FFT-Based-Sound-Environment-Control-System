#include "hr_lcd.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    bool rs;
    uint8_t nibble;
    uint64_t falling_edge_us;
} lcd_latch_t;

typedef struct {
    lcd_latch_t latches[128];
    size_t count;
    size_t writes;
    size_t delays;
    uint64_t now_us;
    uint64_t last_write_us;
    uint64_t rising_edge_us;
    bool rs;
    uint8_t nibble;
    bool enable;
} lcd_trace_t;

static void trace_pins(void *context, bool rs, uint8_t nibble, bool enable)
{
    lcd_trace_t *trace = context;
    assert(nibble <= 15u);
    if (enable || trace->enable) {
        assert(rs == trace->rs);
        assert(nibble == trace->nibble);
    }
    if (trace->writes != 0u && (rs != trace->rs || nibble != trace->nibble)) {
        assert(!trace->enable);
        assert(trace->now_us - trace->last_write_us >= 1u);
    }
    if (enable && !trace->enable) {
        assert(trace->now_us - trace->last_write_us >= 1u);
        trace->rising_edge_us = trace->now_us;
    }
    if (!enable && trace->enable) {
        lcd_latch_t *latch;
        assert(trace->now_us - trace->rising_edge_us >= 1u);
        assert(trace->count < sizeof(trace->latches) / sizeof(trace->latches[0]));
        latch = &trace->latches[trace->count++];
        latch->rs = rs;
        latch->nibble = nibble;
        latch->falling_edge_us = trace->now_us;
    }
    trace->rs = rs;
    trace->nibble = nibble;
    trace->enable = enable;
    trace->last_write_us = trace->now_us;
    ++trace->writes;
}

static void trace_delay(void *context, uint32_t microseconds)
{
    lcd_trace_t *trace = context;
    assert(microseconds != 0u);
    trace->now_us += microseconds;
    ++trace->delays;
}

static void expect_byte(const lcd_trace_t *trace, size_t start,
                        bool rs, uint8_t value)
{
    assert(start + 1u < trace->count);
    assert(trace->latches[start].rs == rs);
    assert(trace->latches[start + 1u].rs == rs);
    assert(trace->latches[start].nibble == (value >> 4));
    assert(trace->latches[start + 1u].nibble == (value & 15u));
}

static void check_render(hr_lcd_t *lcd, lcd_trace_t *trace,
                         const char lines[2][17], const char expected[2][17])
{
    unsigned int row;
    trace->count = 0u;
    hr_lcd_write_lines(lcd, lines);
    assert(trace->count == 68u); /* Two addresses plus 32 characters. */
    for (row = 2u; row < 68u; row += 2u) {
        /* The instruction/data execution wait must separate complete bytes. */
        assert(trace->latches[row].falling_edge_us -
               trace->latches[row - 1u].falling_edge_us >= 60u);
    }
    for (row = 0u; row < 2u; ++row) {
        size_t offset = (size_t)row * 34u;
        unsigned int column;
        expect_byte(trace, offset, false, (uint8_t)(row == 0u ? 0x80u : 0xc0u));
        for (column = 0u; column < 16u; ++column) {
            expect_byte(trace, offset + 2u + 2u * column,
                        true, (uint8_t)expected[row][column]);
        }
    }
    assert(!trace->enable);
}

int test_lcd(void)
{
    lcd_trace_t trace = {0};
    hr_lcd_t lcd = {0};
    hr_lcd_io_t io = {&trace, trace_pins, trace_delay};
    hr_lcd_io_t invalid = {&trace, NULL, trace_delay};
    const char short_lines[2][17] = {"HR 82", "CNT 12"};
    const char padded[2][17] = {"HR 82           ", "CNT 12          "};
    const char empty_lines[2][17] = {"", ""};
    const char blank[2][17] = {"                ", "                "};
    /* Deliberately non-NUL 17th bytes must never reach the LCD. */
    const char full_lines[2][17] = {
        {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','X'},
        {'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','Y'}
    };
    const char clipped[2][17] = {"0123456789ABCDEF", "abcdefghijklmnop"};
    size_t i;
    size_t calls;

    assert(!hr_lcd_init(NULL, io));
    assert(!hr_lcd_init(&lcd, invalid));
    invalid.write_pins = trace_pins;
    invalid.delay_us = NULL;
    assert(!hr_lcd_init(&lcd, invalid));
    hr_lcd_write_lines(&lcd, short_lines);
    hr_lcd_write_lines(NULL, short_lines);
    assert(trace.writes == 0u && trace.delays == 0u);

    assert(hr_lcd_init(&lcd, io));
    assert(lcd.initialized);
    assert(trace.count == 12u);
    assert(trace.latches[0].nibble == 3u);
    assert(trace.latches[1].nibble == 3u);
    assert(trace.latches[2].nibble == 3u);
    assert(trace.latches[3].nibble == 2u);
    for (i = 0u; i < 4u; ++i) {
        assert(!trace.latches[i].rs);
    }
    assert(trace.latches[0].falling_edge_us > 40000u);
    assert(trace.latches[1].falling_edge_us - trace.latches[0].falling_edge_us > 4100u);
    assert(trace.latches[2].falling_edge_us - trace.latches[1].falling_edge_us > 100u);
    assert(trace.latches[3].falling_edge_us - trace.latches[2].falling_edge_us >= 100u);
    expect_byte(&trace, 4u, false, 0x28u);
    expect_byte(&trace, 6u, false, 0x0cu);
    expect_byte(&trace, 8u, false, 0x06u);
    expect_byte(&trace, 10u, false, 0x01u);
    assert(trace.now_us - trace.latches[11].falling_edge_us >= 3000u);
    assert(!trace.enable);

    calls = trace.writes;
    hr_lcd_write_lines(&lcd, NULL);
    assert(trace.writes == calls);
    check_render(&lcd, &trace, short_lines, padded);
    check_render(&lcd, &trace, full_lines, clipped);
    check_render(&lcd, &trace, empty_lines, blank);

    /* A failed reinitialization invalidates the old callbacks for later writes. */
    assert(!hr_lcd_init(&lcd, invalid));
    calls = trace.writes;
    hr_lcd_write_lines(&lcd, short_lines);
    assert(trace.writes == calls);
    puts("LCD: startup, 4-bit transfers, timings and bounded two-line writes PASS");
    return 0;
}
