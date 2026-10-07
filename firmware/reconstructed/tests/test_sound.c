#include "hr_sound.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "sound: line %d: %s\n", __LINE__, #condition); \
    ++failures; } } while (0)

int test_sound(void)
{
    hr_sound_t sound;
    hr_spectrum_t spectrum;
    hr_nlms_t nlms, before;
    uint16_t samples[HR_SOUND_FFT_SIZE];
    char frame[HR_SOUND_FFT_FRAME_CAPACITY];
    size_t i, length;
    int failures = 0;
    double rms_square = 0.0;

    memset(&sound, 0, sizeof(sound));
    memset(samples, 0, sizeof(samples));
    CHECK(!hr_sound_analyze(&sound, samples, &spectrum));
    hr_sound_init(&sound);
    CHECK(hr_sound_analyze(&sound, samples, &spectrum));
    CHECK(spectrum.peak_bin == 0 && spectrum.peak_hz == 0.0f);
    CHECK(spectrum.rms == 0.0f);
    for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
        samples[i] = 2048;
    }
    CHECK(hr_sound_analyze(&sound, samples, &spectrum));
    CHECK(spectrum.rms == 2048.0f && spectrum.peak_bin == 0);
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        CHECK(spectrum.bins[i] == 0);
    }

    /* Independent analytical tone: 1000 Hz, amplitude 700 ADC counts. */
    for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
        samples[i] = (uint16_t)(2048.0 + 700.0 * sin(2.0 * PI * 16.0 * i /
                                                     HR_SOUND_FFT_SIZE) + 0.5);
        rms_square += (double)samples[i] * samples[i];
    }
    CHECK(hr_sound_analyze(&sound, samples, &spectrum));
    CHECK(spectrum.peak_bin == 16 && spectrum.peak_hz == 1000.0f);
    CHECK(abs((int)spectrum.bins[16] - 700) <= 2);
    CHECK(fabs(spectrum.rms - sqrt(rms_square / HR_SOUND_FFT_SIZE)) < 0.001);

    /* Compare every bin with an independent O(N^2) DFT on a broadband signal. */
    {
        double mean = 0.0, window_sum = 0.0;
        uint32_t random_state = 1729u;
        size_t k;
        for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
            random_state = random_state * 1664525u + 1013904223u;
            samples[i] = (uint16_t)((random_state >> 20) & 4095u);
            mean += samples[i];
        }
        mean /= HR_SOUND_FFT_SIZE;
        for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
            window_sum += 0.5 - 0.5 * cos(2.0 * PI * i /
                                         (HR_SOUND_FFT_SIZE - 1u));
        }
        CHECK(hr_sound_analyze(&sound, samples, &spectrum));
        for (k = 0; k < HR_SOUND_BIN_COUNT; ++k) {
            double re = 0.0, im = 0.0;
            int expected;
            for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
                double window = 0.5 - 0.5 * cos(2.0 * PI * i /
                                               (HR_SOUND_FFT_SIZE - 1u));
                double x = ((double)samples[i] - mean) * window;
                re += x * cos(2.0 * PI * k * i / HR_SOUND_FFT_SIZE);
                im -= x * sin(2.0 * PI * k * i / HR_SOUND_FFT_SIZE);
            }
            expected = (int)(sqrt(re * re + im * im) /
                             window_sum * (k == 0 ? 1.0 : 2.0) + 0.5);
            CHECK(abs((int)spectrum.bins[k] - expected) <= 1);
        }
    }

    length = hr_sound_format_fft(&spectrum, frame, sizeof(frame));
    CHECK(length == strlen(frame) && length > 0);
    CHECK(strncmp(frame, "FFT:", 4) == 0);
    CHECK(frame[length - 2] == '\r' && frame[length - 1] == '\n');
    {
        const char *cursor = frame + 4;
        for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
            char *end;
            unsigned long value = strtoul(cursor, &end, 10);
            CHECK(end != cursor && value == spectrum.bins[i]);
            CHECK(*end == (i + 1u == HR_SOUND_BIN_COUNT ? '\r' : ','));
            cursor = end + 1;
        }
    }
    CHECK(hr_sound_format_fft(&spectrum, frame, length) == 0);
    CHECK(frame[0] == '\0');
    CHECK(hr_sound_format_fft(&spectrum, frame, length + 1u) == length);
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        spectrum.bins[i] = UINT16_MAX;
    }
    CHECK(hr_sound_format_fft(&spectrum, frame, sizeof(frame)) ==
          HR_SOUND_FFT_FRAME_CAPACITY - 1u);
    CHECK(hr_sound_format_fft(NULL, frame, sizeof(frame)) == 0 && frame[0] == '\0');
    CHECK(hr_sound_format_fft(&spectrum, NULL, 0) == 0);
    samples[127] = 4096;
    CHECK(!hr_sound_analyze(&sound, samples, &spectrum));
    CHECK(spectrum.rms == 0.0f && spectrum.peak_hz == 0.0f);
    CHECK(!hr_sound_analyze(NULL, samples, &spectrum));
    CHECK(!hr_sound_analyze(&sound, NULL, &spectrum));
    CHECK(!hr_sound_analyze(&sound, samples, NULL));
    hr_sound_init(NULL);

    CHECK(!hr_nlms_init(&nlms, 0, 0.1f));
    CHECK(!hr_nlms_init(&nlms, 33, 0.1f));
    CHECK(!hr_nlms_init(&nlms, 1, 0.0f));
    CHECK(!hr_nlms_init(&nlms, 1, 2.0f));
    CHECK(!hr_nlms_init(&nlms, 1, NAN));
    CHECK(!hr_nlms_init(NULL, 1, 0.1f));
    CHECK(hr_nlms_init(&nlms, 1, 0.5f));
    CHECK(hr_nlms_process(&nlms, 1.0f, 0.75f) == 0.75f);
    for (i = 0; i < 100; ++i) {
        hr_nlms_process(&nlms, 1.0f, 0.75f);
    }
    CHECK(fabsf(hr_nlms_process(&nlms, 1.0f, 0.75f)) < 0.00001f);
    CHECK(fabsf(nlms.weights[0] - 0.75f) < 0.00001f);

    /* Identify a delayed two-tap plant from deterministic white input. */
    {
        uint32_t random_state = 42u;
        float previous = 0.0f;
        double initial_error = 0.0, final_error = 0.0;
        CHECK(hr_nlms_init(&nlms, 2, 0.4f));
        for (i = 0; i < 2048; ++i) {
            float reference, desired, error;
            random_state = random_state * 1664525u + 1013904223u;
            reference = (float)((int)(random_state >> 16) - 32768) / 32768.0f;
            desired = 0.6f * reference - 0.25f * previous;
            error = hr_nlms_process(&nlms, reference, desired);
            if (i < 64) initial_error += (double)error * error;
            if (i >= 1984) final_error += (double)error * error;
            previous = reference;
        }
        CHECK(final_error < initial_error * 0.0001);
        CHECK(fabsf(nlms.weights[0] - 0.6f) < 0.0001f);
        CHECK(fabsf(nlms.weights[1] + 0.25f) < 0.0001f);
    }
    before = nlms;
    CHECK(hr_nlms_process(&nlms, NAN, 1.0f) == 0.0f);
    CHECK(memcmp(&before, &nlms, sizeof(nlms)) == 0);
    CHECK(hr_nlms_process(&nlms, 1.0f, INFINITY) == 0.0f);
    CHECK(memcmp(&before, &nlms, sizeof(nlms)) == 0);
    CHECK(hr_nlms_process(NULL, 1.0f, 1.0f) == 0.0f);
    CHECK(hr_nlms_init(&nlms, HR_NLMS_MAX_TAPS, 0.1f));
    CHECK(isfinite(hr_nlms_process(&nlms, FLT_MAX, FLT_MAX)));
    CHECK(isfinite(hr_nlms_process(&nlms, 0.0f, 0.0f)));
    return failures;
}
