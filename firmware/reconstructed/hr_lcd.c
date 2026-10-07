#include "hr_lcd.h"

#include <stddef.h>

/* HD44780U data sheet: instruction timings (Table 6), 4-bit startup
 * (Figure 24), and 2.7 V write timing. Fixed waits also allow for the slower
 * specified oscillator. R/W is grounded, so the busy flag is not read.
 * https://cdn-shop.adafruit.com/datasheets/HD44780.pdf
 */
static void write_nibble(hr_lcd_t *lcd, bool rs, uint8_t nibble)
{
    nibble &= 0x0fu;
    lcd->io.write_pins(lcd->io.ctx, rs, nibble, false);
    lcd->io.delay_us(lcd->io.ctx, 1u);
    lcd->io.write_pins(lcd->io.ctx, rs, nibble, true);
    lcd->io.delay_us(lcd->io.ctx, 1u);
    lcd->io.write_pins(lcd->io.ctx, rs, nibble, false);
    lcd->io.delay_us(lcd->io.ctx, 1u);
}

static void write_byte(hr_lcd_t *lcd, bool rs, uint8_t value)
{
    write_nibble(lcd, rs, (uint8_t)(value >> 4));
    write_nibble(lcd, rs, value);
    lcd->io.delay_us(lcd->io.ctx,
                     (!rs && (value == 0x01u || value == 0x02u))
                         ? 3000u : 100u);
}

bool hr_lcd_init(hr_lcd_t *lcd, hr_lcd_io_t io)
{
    if (lcd == NULL) {
        return false;
    }
    lcd->initialized = false;
    if (io.write_pins == NULL || io.delay_us == NULL) {
        return false;
    }
    lcd->io = io;
    lcd->io.write_pins(lcd->io.ctx, false, 0u, false);
    lcd->io.delay_us(lcd->io.ctx, 50000u);

    /* These are single nibble writes while the interface may still be 8-bit. */
    write_nibble(lcd, false, 0x03u);
    lcd->io.delay_us(lcd->io.ctx, 5000u);
    write_nibble(lcd, false, 0x03u);
    lcd->io.delay_us(lcd->io.ctx, 150u);
    write_nibble(lcd, false, 0x03u);
    lcd->io.delay_us(lcd->io.ctx, 150u);
    write_nibble(lcd, false, 0x02u);
    lcd->io.delay_us(lcd->io.ctx, 100u);

    write_byte(lcd, false, 0x28u); /* 4-bit interface, 2 lines, 5x8 font. */
    write_byte(lcd, false, 0x0cu); /* Display on, cursor and blink off. */
    write_byte(lcd, false, 0x06u); /* Increment address, no display shift. */
    write_byte(lcd, false, 0x01u); /* Clear DDRAM and reset the address. */
    lcd->initialized = true;
    return true;
}

void hr_lcd_write_lines(hr_lcd_t *lcd, const char lines[2][17])
{
    unsigned int row;
    if (lcd == NULL || !lcd->initialized || lines == NULL) {
        return;
    }
    for (row = 0u; row < 2u; ++row) {
        unsigned int column;
        bool ended = false;
        write_byte(lcd, false, (uint8_t)(row == 0u ? 0x80u : 0xc0u));
        for (column = 0u; column < 16u; ++column) {
            uint8_t character = (uint8_t)' ';
            if (!ended) {
                character = (uint8_t)lines[row][column];
                if (character == 0u) {
                    ended = true;
                    character = (uint8_t)' ';
                }
            }
            write_byte(lcd, true, character);
        }
    }
}
