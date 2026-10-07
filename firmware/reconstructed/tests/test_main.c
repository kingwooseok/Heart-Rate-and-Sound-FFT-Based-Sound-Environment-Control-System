#include "hr_sound.h"
#include <math.h>
#include <stdio.h>

int test_control(void);
int test_sound(void);
int test_lcd(void);
int test_stm32(void);

int main(void) {
    int failures = 0;
    unsigned i;
    hr_sound_t sound;
    hr_spectrum_t spectrum;
    uint16_t samples[128];
    char frame[HR_SOUND_FFT_FRAME_CAPACITY];
    failures += test_control();
    failures += test_sound();
    failures += test_lcd();
    failures += test_stm32();
    if (failures != 0) {
        fprintf(stderr, "FAIL: %d firmware test(s)\n", failures);
        return 1;
    }
    /* Feed this real C-generated frame to the existing Python viewer parser. */
    hr_sound_init(&sound);
    for (i = 0; i < 128; ++i)
        samples[i] = (uint16_t)(2048.0 + 500.0 * sin(6.283185307179586 * 16.0 * i / 128.0));
    if (!hr_sound_analyze(&sound, samples, &spectrum) ||
        hr_sound_format_fft(&spectrum, frame, sizeof(frame)) == 0) return 1;
    puts("PASS: control, DSP, LCD and three-node HAL model");
    fputs(frame, stdout);
    return 0;
}
