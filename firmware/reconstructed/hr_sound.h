#ifndef HR_SOUND_H
#define HR_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reimplementation from the documented data format; original DSP source absent. */
#define HR_SOUND_FFT_SIZE 128u
#define HR_SOUND_BIN_COUNT 64u
#define HR_SOUND_SAMPLE_RATE_HZ 8000u
#define HR_SOUND_ADC_MAX 4095u
/* Includes the terminating NUL; transmitted frames do not include that NUL. */
#define HR_SOUND_FFT_FRAME_CAPACITY 390u
#define HR_NLMS_MAX_TAPS 32u

typedef struct {
    uint16_t bins[HR_SOUND_BIN_COUNT];
    uint8_t peak_bin;
    float peak_hz;
    /* RMS of the original ADC values, including their DC bias, in ADC counts. */
    float rms;
} hr_spectrum_t;

typedef struct {
    float window[HR_SOUND_FFT_SIZE];
    float twiddle_real[HR_SOUND_BIN_COUNT];
    float twiddle_imag[HR_SOUND_BIN_COUNT];
    float real[HR_SOUND_FFT_SIZE];
    float imag[HR_SOUND_FFT_SIZE];
    float window_sum;
    bool initialized;
} hr_sound_t;

void hr_sound_init(hr_sound_t *state);
/*
 * samples must contain 128 consecutive samples acquired at 8000 Hz.
 * Removes the block mean, applies a symmetric Hann window, and reports
 * single-sided amplitudes corrected for the window's coherent gain.
 * Bins are rounded/saturated to uint16_t ADC counts; the Nyquist bin is omitted.
 * The strongest non-DC bin supplies peak_hz; an all-zero spectrum returns 0 Hz.
 * Invalid input (including samples >4095) returns false and clears *spectrum.
 * Call init first; each concurrent caller requires a separate state.
 */
bool hr_sound_analyze(hr_sound_t *state,
                      const uint16_t samples[HR_SOUND_FFT_SIZE],
                      hr_spectrum_t *spectrum);
/* Returns bytes excluding NUL. On insufficient capacity returns 0/empty string. */
size_t hr_sound_format_fft(const hr_spectrum_t *spectrum,
                           char *buffer, size_t capacity);

/*
 * Algorithm reference only: normalized LMS adaptive FIR noise estimation.
 * This does not implement speaker output, secondary-path compensation, or ANC.
 */
typedef struct {
    float weights[HR_NLMS_MAX_TAPS];
    float history[HR_NLMS_MAX_TAPS];
    float mu;
    uint8_t taps;
    bool initialized;
} hr_nlms_t;

/* Valid settings: 1..32 taps, finite 0 < mu < 2. Clears coefficients/history. */
bool hr_nlms_init(hr_nlms_t *state, size_t taps, float mu);
/*
 * Returns desired - estimated(reference), then adapts the FIR coefficients.
 * Nonfinite input or an unrepresentable result returns 0 without state change.
 * Finite inputs are caller-scaled; energy regularization is 1e-6.
 */
float hr_nlms_process(hr_nlms_t *state, float reference, float desired);

#ifdef __cplusplus
}
#endif
#endif
