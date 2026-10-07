#ifndef HR_LCD_H
#define HR_LCD_H

#include <stdbool.h>
#include <stdint.h>

/* Reconstructed driver for a 16x2, HD44780-compatible, 4-bit parallel LCD.
 * Connect R/W to ground. The original LCD controller and wiring are unknown.
 */
typedef struct {
    void *ctx;
    /* nibble bits 0..3 map to LCD DB4..DB7. RS and data must be updated
     * while E is low; when E is high, preserve those existing pin levels.
     */
    void (*write_pins)(void *ctx, bool rs, uint8_t nibble, bool enable);
    /* Wait at least the requested number of microseconds; never round down. */
    void (*delay_us)(void *ctx, uint32_t microseconds);
} hr_lcd_io_t;

typedef struct {
    hr_lcd_io_t io;
    bool initialized;
} hr_lcd_t;

/* Call after the LCD supply is stable and GPIO outputs are configured.
 * A true result confirms valid callbacks and completion of the write sequence;
 * the write-only interface does not read a hardware acknowledgement.
 */
bool hr_lcd_init(hr_lcd_t *lcd, hr_lcd_io_t io);

/* Write both 16-character rows, including spaces after the first NUL in each.
 * Reads at most 16 bytes per row; row[16] need not be NUL. Calls with NULL or an
 * uninitialized LCD are ignored. Call from foreground code, not an ISR.
 */
void hr_lcd_write_lines(hr_lcd_t *lcd, const char lines[2][17]);

#endif
