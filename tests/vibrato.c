// Check the pitch modulation used by the actual DSP, including dry restoration.
#include "../lib/MonkDSP/src/monk.c"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    monk_init();
    S.v[0].vibrato_depth = 1.0f;
    update_vibrato(&S.v[0]);
    const mk_voice_t original = S.v[0];
    int32_t baseline[2048];
    for (unsigned i = 0; i < 2048; ++i)
        baseline[i] = compute_vibrato(&S.v[0]);

    S.v[0] = original;
    const uint32_t rate = S.v[0].vibrato_inc;
    monk_vibrato_boost(32768);
    assert(S.v[0].vibrato_inc == rate);
    int32_t low = 0, high = 0;
    for (unsigned i = 0; i < 22050; ++i) {
        const int32_t bend = compute_vibrato(&S.v[0]);
        if (bend < low) low = bend;
        if (bend > high) high = bend;
    }
    assert(high > 3.99f * PITCH_SCALE && high <= 4.0f * PITCH_SCALE);
    assert(low < -3.99f * PITCH_SCALE && low >= -4.0f * PITCH_SCALE);

    S.v[0] = original;
    monk_vibrato_boost(0);
    for (unsigned i = 0; i < 2048; ++i)
        assert(compute_vibrato(&S.v[0]) == baseline[i]);
    monk_vibrato_boost(65535);
    assert(vibrato_boost <= 2.8f);
    monk_init();
    assert(vibrato_boost == 0.0f);
    puts("Vibrato: ±4-semitone depth, unchanged rate, sample-exact zero boost and reset passed.");
}
