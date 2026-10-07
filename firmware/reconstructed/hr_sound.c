#include "hr_sound.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define HR_PI 3.14159265358979323846

void hr_sound_init(hr_sound_t *state)
{
    size_t i;
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
        state->window[i] = (float)(0.5 - 0.5 * cos(2.0 * HR_PI * i /
                                                  (HR_SOUND_FFT_SIZE - 1u)));
        state->window_sum += state->window[i];
    }
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        double angle = -2.0 * HR_PI * i / HR_SOUND_FFT_SIZE;
        state->twiddle_real[i] = (float)cos(angle);
        state->twiddle_imag[i] = (float)sin(angle);
    }
    state->initialized = true;
}

static size_t reverse_seven_bits(size_t value)
{
    size_t reversed = 0;
    unsigned bit;
    for (bit = 0; bit < 7; ++bit) {
        reversed = (reversed << 1) | (value & 1u);
        value >>= 1;
    }
    return reversed;
}

bool hr_sound_analyze(hr_sound_t *state,
                      const uint16_t samples[HR_SOUND_FFT_SIZE],
                      hr_spectrum_t *spectrum)
{
    size_t i, length;
    double sum = 0.0, sum_squares = 0.0;
    float mean, peak = 0.0f;
    if (spectrum == NULL) {
        return false;
    }
    memset(spectrum, 0, sizeof(*spectrum));
    if (state == NULL || samples == NULL || !state->initialized ||
        !isfinite(state->window_sum) || state->window_sum <= 0.0f) {
        return false;
    }
    for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
        if (samples[i] > HR_SOUND_ADC_MAX) {
            return false;
        }
        sum += samples[i];
        sum_squares += (double)samples[i] * samples[i];
    }
    mean = (float)(sum / HR_SOUND_FFT_SIZE);
    for (i = 0; i < HR_SOUND_FFT_SIZE; ++i) {
        size_t reversed = reverse_seven_bits(i);
        state->real[reversed] = ((float)samples[i] - mean) * state->window[i];
        state->imag[reversed] = 0.0f;
    }
    for (length = 2; length <= HR_SOUND_FFT_SIZE; length <<= 1) {
        size_t base, half = length / 2;
        size_t stride = HR_SOUND_FFT_SIZE / length;
        for (base = 0; base < HR_SOUND_FFT_SIZE; base += length) {
            for (i = 0; i < half; ++i) {
                size_t left = base + i, right = left + half;
                float wr = state->twiddle_real[i * stride];
                float wi = state->twiddle_imag[i * stride];
                float tr = wr * state->real[right] - wi * state->imag[right];
                float ti = wr * state->imag[right] + wi * state->real[right];
                float lr = state->real[left], li = state->imag[left];
                state->real[left] = lr + tr;
                state->imag[left] = li + ti;
                state->real[right] = lr - tr;
                state->imag[right] = li - ti;
            }
        }
    }
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        float magnitude = hypotf(state->real[i], state->imag[i]) /
                          state->window_sum;
        if (i != 0) {
            magnitude *= 2.0f;
        }
        if (!isfinite(magnitude)) {
            memset(spectrum, 0, sizeof(*spectrum));
            return false;
        }
        spectrum->bins[i] = magnitude >= (float)UINT16_MAX
                                ? UINT16_MAX
                                : (uint16_t)(magnitude + 0.5f);
        if (i != 0 && magnitude > peak) {
            peak = magnitude;
            spectrum->peak_bin = (uint8_t)i;
        }
    }
    spectrum->peak_hz = (float)spectrum->peak_bin *
                        (float)HR_SOUND_SAMPLE_RATE_HZ / HR_SOUND_FFT_SIZE;
    spectrum->rms = (float)sqrt(sum_squares / HR_SOUND_FFT_SIZE);
    return true;
}

static size_t decimal_digits(uint16_t value)
{
    size_t count = 1;
    while (value >= 10u) {
        value /= 10u;
        ++count;
    }
    return count;
}

size_t hr_sound_format_fft(const hr_spectrum_t *spectrum,
                           char *buffer, size_t capacity)
{
    size_t i, length = 4u + (HR_SOUND_BIN_COUNT - 1u) + 2u;
    char *output;
    if (buffer == NULL || capacity == 0) {
        return 0;
    }
    buffer[0] = '\0';
    if (spectrum == NULL) {
        return 0;
    }
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        length += decimal_digits(spectrum->bins[i]);
    }
    if (capacity <= length) {
        return 0;
    }
    memcpy(buffer, "FFT:", 4u);
    output = buffer + 4u;
    for (i = 0; i < HR_SOUND_BIN_COUNT; ++i) {
        uint16_t value = spectrum->bins[i];
        size_t digits = decimal_digits(value), digit = digits;
        do {
            output[--digit] = (char)('0' + value % 10u);
            value /= 10u;
        } while (digit != 0);
        output += digits;
        if (i + 1u != HR_SOUND_BIN_COUNT) {
            *output++ = ',';
        }
    }
    *output++ = '\r';
    *output++ = '\n';
    *output = '\0';
    return length;
}

bool hr_nlms_init(hr_nlms_t *state, size_t taps, float mu)
{
    if (state == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    if (taps == 0 || taps > HR_NLMS_MAX_TAPS ||
        !isfinite(mu) || mu <= 0.0f || mu >= 2.0f) {
        return false;
    }
    state->mu = mu;
    state->taps = (uint8_t)taps;
    state->initialized = true;
    return true;
}

float hr_nlms_process(hr_nlms_t *state, float reference, float desired)
{
    float history[HR_NLMS_MAX_TAPS], weights[HR_NLMS_MAX_TAPS];
    double estimate = 0.0, energy = 1e-6, residual, gain;
    size_t i;
    if (state == NULL || !state->initialized ||
        state->taps == 0 || state->taps > HR_NLMS_MAX_TAPS ||
        !isfinite(state->mu) || state->mu <= 0.0f || state->mu >= 2.0f ||
        !isfinite(reference) || !isfinite(desired)) {
        return 0.0f;
    }
    history[0] = reference;
    for (i = 1; i < state->taps; ++i) {
        history[i] = state->history[i - 1u];
    }
    for (i = 0; i < state->taps; ++i) {
        if (!isfinite(history[i]) || !isfinite(state->weights[i])) {
            return 0.0f;
        }
        estimate += (double)history[i] * state->weights[i];
        energy += (double)history[i] * history[i];
    }
    residual = (double)desired - estimate;
    if (!isfinite(residual) || fabs(residual) > FLT_MAX) {
        return 0.0f;
    }
    gain = (double)state->mu * residual / energy;
    for (i = 0; i < state->taps; ++i) {
        double weight = (double)state->weights[i] + gain * history[i];
        if (!isfinite(weight) || fabs(weight) > FLT_MAX) {
            return 0.0f;
        }
        weights[i] = (float)weight;
    }
    memcpy(state->history, history, state->taps * sizeof(float));
    memcpy(state->weights, weights, state->taps * sizeof(float));
    return (float)residual;
}
