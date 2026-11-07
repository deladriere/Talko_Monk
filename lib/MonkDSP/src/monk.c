/* SPDX-License-Identifier: MIT AND GPL-3.0-only
 *
 * MonkSynth's DSP (dsp/voice.c, synth.c, delay.c), Copyright (c) 2026 Jonathan Taylor, MIT License:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 *   associated documentation files (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge, publish, distribute,
 *   sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions: The above copyright notice and this
 *   permission notice shall be included in all copies or substantial portions of the Software.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * Ported to the FM-1 for MONK-1 (GPL-3.0-only as a whole). What changed, and why:
 *   - memory: upstream's voice is 168 KB (its tables, sized for 192 kHz, in every voice). The tables
 *     are shared here and sized for 44.1 kHz; the overlap buffer is 2048 (a grain lands at most a
 *     block ahead of the read, and is 882 long); the delay runs at half rate in int16 (70 KB).
 *   - speed: the grain is recomputed only when what it is made of changes (vowel, head size,
 *     breath); upstream rebuilt the same grain every pitch period. The decay and the window are
 *     multiplied into one table per formant. The pitch's 2^(k/384) comes from a table and is
 *     looked up only when the quantized pitch step changes (upstream: a powf every sample).
 *   - pitch in note units throughout (no hz <-> note round trips: no libm here), and velocity:
 *     upstream ignores it; here it scales the level by `vel_sens`.
 *   - the wrapper of the Schwung port: knob and pressure slewed per block, pressure added to the
 *     vowel or bending the pitch.
 * The sound otherwise follows upstream step for step: the grain, the 32-steps-a-semitone period,
 * the vibrato with its jitter, the two glides, the ADSR, the unison spread, the delay's taps. */
#include "monk.h"
#include "fastmath.h"

/* The RP2040 linker puts zero-initialized DSP storage in ordinary BSS. */
#define MK_POOL

#define NUM_FORMANTS 3
#define SPLINE_SIZE 1280
#define SPLINE_SEG 320
#define SINE_SIZE 1024
#define GRAIN_LEN 441                 /* 0.02 s at 22.05 kHz */
#define OLAP 1024u                    /* > a block (128) + a grain */
#define MAX_UNISON 3                  /* first Talko prototype */
#define MAX_NOTES 16
#define BLOCK 128u
#define PITCH_TABLE_BASE 8.175799f    /* MIDI note 0 */
#define ENV_ANTICLICK 0.003f
#define RAMP_TICKS 10

static const float FORMANT_FREQS[NUM_FORMANTS][5] = {
    {280.0f, 450.0f, 800.0f, 350.0f, 270.0f},
    {600.0f, 800.0f, 1150.0f, 2000.0f, 2140.0f},
    {2240.0f, 2830.0f, 2900.0f, 2800.0f, 2950.0f},
};
static const float FORMANT_BW[NUM_FORMANTS] = {32.5f, 47.5f, 62.5f};

/* ------------------------------------------------------------ the shared tables */
/* Grains and the overlap buffer use Q14. Sine and formant windows use Q15. */
#define SINE_SCALE 32767
#define GRAIN_SCALE 16384
#define PHASE_FRAC_BITS 16
#define CLIP_STEPS 512
#define CLIP_FRAC_BITS 8
#define PITCH_FRAC_BITS 10
#define PITCH_SCALE (32 << PITCH_FRAC_BITS)
#define PITCH_STEP_MIN (-38 * 32)
#define PITCH_STEP_MAX (129 * 32)
#define ENV_SCALE (1 << 24)
#define GAIN_SCALE (1 << 16)
#define EXCITATION_BITS 14
#define EXCITATION_SCALE (1 << EXCITATION_BITS)

static int16_t sine_tbl[SINE_SIZE] MK_POOL;
static float vibrato_boost;
static int16_t clip_tbl[CLIP_STEPS + 1] MK_POOL;
static float formant_tbl[NUM_FORMANTS][SPLINE_SIZE] MK_POOL;
static int16_t env_tbl[NUM_FORMANTS][GRAIN_LEN] MK_POOL;   /* the formant's decay times the window */
static int16_t asp_tbl[GRAIN_LEN] MK_POOL;                 /* the aspiration times the window */
static uint16_t period_tbl[PITCH_STEP_MAX - PITCH_STEP_MIN + 1];

enum { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    uint8_t active;
    float current_pitch, target_pitch;   /* note units */
    float pitch_bend_offset;
    float glide_param, min_glide;
    float pitch_step;
    int pitch_ramp_ticks;
    float vowel_step;
    int vowel_ramp_ticks;
    int ramp_counter, ramp_period;
    uint32_t vibrato_phase, vibrato_inc;
    int32_t vibrato_amount;
    float vibrato_depth, vibrato_rate, rate_scale;
    float random_jitter;
    uint32_t jitter_counter, jitter_period;
    uint32_t rng_state, noise_rng, noise_period;
    uint32_t overlap_write_pos, overlap_read_pos, overlap_offset;
    float current_vowel, target_vowel, current_voice, aspiration_amp;
    uint8_t grain_dirty;
    float g_vowel, g_voice, g_asp;       /* what the grain in grain[] was made of (-1: none yet) */
    int env_stage;
    float env_attack, env_decay, env_release;
    int32_t env_level, env_sustain;
    int32_t attack_step, decay_step, release_step;
    int32_t overlap_buf[OLAP];
    int32_t grain[GRAIN_LEN];
} mk_voice_t;

typedef struct {
    int16_t l[8832], r[8832];          /* half rate: the longest tap (rate 1) is 8785 */
    uint32_t w;
    int32_t target_l, target_r, cur_l, cur_r;  /* delay positions in Q16 */
    float rate;
    int32_t mix;                             /* feedback/input mix in Q16 */
    int32_t tap_l, tap_r;                     /* last half-rate taps in Q14 */
} mk_delay_t;
#define DL_N 8832u
#define DL_FRAC_BITS 16
#define DL_FRAC_SCALE (1u << DL_FRAC_BITS)
#define DL_SMOOTH_STEP ((int32_t)(2.0f * DL_FRAC_SCALE / (0.05f * MK_FS) + 0.5f))
#define DL_SAMPLE_SHIFT 4                 /* delay samples stored in Q10 */

static struct {
    mk_voice_t v[MAX_UNISON];
    int unison_count;
    int32_t growl;
    float unison_detune, unison_voice_spread, base_voice, last_base_note;
    uint8_t held[MAX_NOTES], held_count;
    float cc_volume, level, cur_gain, target_gain;
} S MK_POOL;
static mk_delay_t D MK_POOL;

volatile float monk_ui_vowel = 0.5f, monk_ui_amp;
volatile uint8_t monk_ui_active;

static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static float clamp01(float v) { return clampf(v, 0.0f, 1.0f); }

/* ------------------------------------------------------------------- tables */
static void build_tables(void)
{
    int f, seg, i;
    for (i = 0; i < SINE_SIZE; i++)
        sine_tbl[i] = (int16_t)(SINE_SCALE * fm_sinf(FM_TWO_PI * (float)i / SINE_SIZE));
    for (i = 0; i <= CLIP_STEPS; i++) {
        float y = fm_minf(fm_tanhf((float)i * (4.0f / CLIP_STEPS)), 0.999f);
        clip_tbl[i] = (int16_t)(y * 32768.0f + 0.5f);
    }
    for (f = 0; f < NUM_FORMANTS; f++) {         /* Catmull-Rom across the five vowels */
        const float *pts = FORMANT_FREQS[f];
        float padded[7] = {pts[0], pts[0], pts[1], pts[2], pts[3], pts[4], pts[4]};
        for (seg = 0; seg < 4; seg++) {
            float p0 = padded[seg], p1 = padded[seg + 1], p2 = padded[seg + 2], p3 = padded[seg + 3];
            for (i = 0; i < SPLINE_SEG; i++) {
                float t = (float)i / SPLINE_SEG, t2 = t * t, t3 = t2 * t;
                formant_tbl[f][seg * SPLINE_SEG + i] =
                    0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
            }
        }
    }
    {   /* the grain window: a short cosine attack, flat, a cosine release that stops short of zero */
        float sr = MK_FS, bw = 50.0f * FM_PI, p1 = sr * 0.000202f, p2 = sr * 0.000263f;
        uint32_t attack_len = (uint32_t)(sr * 0.0018f), release_start = (uint32_t)(sr * 0.013f);
        uint32_t release_len = (uint32_t)(sr * 0.007f), decay_len = GRAIN_LEN * 4u, n;
        for (n = 0; n < GRAIN_LEN; n++) {
            float w = 1.0f, s1, s2, d1 = 0.0f, d2 = 0.0f;
            uint32_t i1, i2;
            if (n < attack_len)
                w = 0.5f * (1.0f - fm_cosf(FM_PI * (float)n / (float)attack_len));
            if (n >= release_start)
                w = 0.5f * (1.0f - fm_cosf(FM_PI * (float)(release_len + n) / (float)release_len));
            for (f = 0; f < NUM_FORMANTS; f++) {
                uint32_t di = (uint32_t)(FORMANT_BW[f] * (float)n / 50.0f);
                float envelope = di < decay_len ? fm_expf(-bw * (float)di / sr) : 0.0f;
                env_tbl[f][n] = (int16_t)(envelope * w * SINE_SCALE);
            }
            s1 = fm_sinf(FM_TWO_PI * (float)n / p1);   /* the breath: two inharmonic decaying sines */
            i1 = (uint32_t)((float)n * 3.0f);
            if (i1 < decay_len)
                d1 = fm_expf(-bw * (float)i1 / sr);
            s2 = fm_sinf(FM_TWO_PI * (float)n / p2);
            i2 = (uint32_t)((float)n * 3.5f);
            if (i2 < decay_len)
                d2 = fm_expf(-bw * (float)i2 / sr);
            asp_tbl[n] = (int16_t)((s1 * d1 + s2 * d2) * w * GRAIN_SCALE);
        }
    }
    /* One period per 1/32 semitone across the MIDI range, bend and vibrato. */
    for (i = PITCH_STEP_MIN; i <= PITCH_STEP_MAX; i++) {
        int oct = i >= 0 ? i / 384 : -((-i + 383) / 384);
        int step = i - oct * 384;
        float freq = fm_exp2f((float)step / 384.0f) * PITCH_TABLE_BASE;
        freq = oct >= 0 ? freq * (float)(1u << oct) : freq / (float)(1u << -oct);
        period_tbl[i - PITCH_STEP_MIN] = (uint16_t)(MK_FS / freq);
    }
}

/* ------------------------------------------------------------------- voice */
static float next_rand(mk_voice_t *v)
{
    v->rng_state = v->rng_state * 1103515245u + 12345u;
    return (float)((v->rng_state >> 16) & 0x7FFF) / 32768.0f;
}

static void update_decay_step(mk_voice_t *v)
{
    float rate = v->env_decay > 1.0f / MK_FS ? 1.0f / (v->env_decay * MK_FS) : 1.0f;
    v->decay_step = (int32_t)(rate * (float)(ENV_SCALE - v->env_sustain) + 0.5f);
    if (v->decay_step == 0 && v->env_sustain < ENV_SCALE)
        v->decay_step = 1;
}

static int32_t env_tick(mk_voice_t *v)
{
    switch (v->env_stage) {
    case ENV_ATTACK:
        v->env_level += v->attack_step;
        if (v->env_level >= ENV_SCALE) {
            v->env_level = ENV_SCALE;
            v->env_stage = v->env_decay > 0.0f ? ENV_DECAY : ENV_SUSTAIN;
        }
        break;
    case ENV_DECAY:
        v->env_level -= v->decay_step;
        if (v->env_level <= v->env_sustain) {
            v->env_level = v->env_sustain;
            v->env_stage = ENV_SUSTAIN;
        }
        break;
    case ENV_RELEASE:
        v->env_level -= v->release_step;
        if (v->env_level <= 0) {
            v->env_level = 0;
            v->env_stage = ENV_IDLE;
            v->active = 0;
        }
        break;
    default:
        break;
    }
    return v->env_level;
}

static float lookup_formant(int f, float vowel)
{
    float pos = clampf(vowel, 0.0f, 1.0f) * (SPLINE_SIZE - 1), frac;
    int idx = (int)pos;
    if (idx > SPLINE_SIZE - 2)
        idx = SPLINE_SIZE - 2;
    frac = pos - (float)idx;
    return formant_tbl[f][idx] * (1.0f - frac) + formant_tbl[f][idx + 1] * frac;
}

static uint32_t grain_period(int32_t pitch)
{
    int step = pitch / (1 << PITCH_FRAC_BITS);
    if (step < PITCH_STEP_MIN)
        step = PITCH_STEP_MIN;
    if (step > PITCH_STEP_MAX)
        step = PITCH_STEP_MAX;
    return period_tbl[step - PITCH_STEP_MIN];
}

static int32_t base_pitch(const mk_voice_t *v)
{
    return (int32_t)((v->current_pitch + v->pitch_bend_offset - 12.0f) * PITCH_SCALE);
}

uint32_t monk_pitch_period(void)
{
    return grain_period(base_pitch(&S.v[0]));
}

static void apply_portamento(mk_voice_t *v)
{
    if (v->min_glide > 0.001f) {
        if (--v->ramp_counter > 0)
            return;
        v->ramp_counter = v->ramp_period;
        if (v->pitch_ramp_ticks > 0) {
            v->pitch_ramp_ticks--;
            v->current_pitch += v->pitch_step;
            if (v->pitch_ramp_ticks == 0)
                v->current_pitch = v->target_pitch;
        }
        if (v->vowel_ramp_ticks > 0) {
            v->vowel_ramp_ticks--;
            v->current_vowel += v->vowel_step;
            if (v->vowel_ramp_ticks == 0)
                v->current_vowel = v->target_vowel;
        }
    } else if (v->glide_param > 0.001f) {
        float diff = v->target_pitch - v->current_pitch;
        float rate = 12.0f / ((v->glide_param + 0.01f) * MK_FS);
        if (diff > 0.2f)
            v->current_pitch += rate;
        else if (diff < -0.2f)
            v->current_pitch -= rate;
        else
            v->current_pitch = v->target_pitch;
        v->current_vowel = v->target_vowel;
    } else {
        v->current_pitch = v->target_pitch;
        v->current_vowel = v->target_vowel;
    }
}

static void update_vibrato(mk_voice_t *v)
{
    const float rate = (v->vibrato_depth * 0.2f + 1.0f) * v->random_jitter * v->rate_scale;
    v->vibrato_inc = (uint32_t)(rate * (4294967296.0f / MK_FS));
    v->vibrato_amount = (int32_t)((v->vibrato_depth + 0.2f + vibrato_boost) * PITCH_SCALE);
}

static int32_t compute_vibrato(mk_voice_t *v)
{
    v->vibrato_phase += v->vibrato_inc;
    if (++v->jitter_counter >= v->jitter_period) {
        v->jitter_counter = 0;
        v->random_jitter = next_rand(v) * 2.0f + 5.0f;
        update_vibrato(v);
    }
    const uint32_t index = v->vibrato_phase >> 22; /* 1024 table entries */
    return (int32_t)(((int64_t)v->vibrato_amount * sine_tbl[index]) >> 15);
}

static uint32_t formant_step(int formant, const mk_voice_t *v, float head_scale)
{
    return (uint32_t)(lookup_formant(formant, v->current_vowel) * head_scale *
                      ((float)(SINE_SIZE << PHASE_FRAC_BITS) / MK_FS) + 0.5f);
}

static int32_t formant_sample(uint32_t phase, int16_t envelope)
{
    uint32_t index = (phase >> PHASE_FRAC_BITS) & (SINE_SIZE - 1);
    return (sine_tbl[index] * (int32_t)envelope) >> 16;
}

static void compute_grain(mk_voice_t *v)
{
    const float head_scale = v->current_voice * 0.5f + 0.75f;
    const uint32_t k0 = formant_step(0, v, head_scale);
    const uint32_t k1 = formant_step(1, v, head_scale);
    const uint32_t k2 = formant_step(2, v, head_scale);
    const int32_t aspiration = (int32_t)(v->aspiration_amp * SINE_SCALE + 0.5f);
    uint32_t p0 = 0, p1 = 0, p2 = 0, i;
    for (i = 0; i < GRAIN_LEN; i++) {
        v->grain[i] = formant_sample(p0, env_tbl[0][i]) +
                      formant_sample(p1, env_tbl[1][i]) +
                      formant_sample(p2, env_tbl[2][i]) +
                      ((asp_tbl[i] * aspiration) >> 15);
        p0 += k0;
        p1 += k1;
        p2 += k2;
    }
    v->g_vowel = v->current_vowel;
    v->g_voice = v->current_voice;
    v->g_asp = v->aspiration_amp;
    v->grain_dirty = 0;
}

static void add_grain(int32_t *dst, const int32_t *grain, uint32_t count, int32_t gain)
{
    if (gain == EXCITATION_SCALE) {
        for (uint32_t i = 0; i < count; i++)
            dst[i] += grain[i];
    } else {
        for (uint32_t i = 0; i < count; i++)
            dst[i] += (grain[i] * gain) >> EXCITATION_BITS;
    }
}

static void overlap_add(mk_voice_t *v, int32_t gain)
{
    v->overlap_write_pos = (v->overlap_write_pos + v->overlap_offset) & (OLAP - 1u);
    uint32_t pos = v->overlap_write_pos;
    uint32_t first = OLAP - pos < GRAIN_LEN ? OLAP - pos : GRAIN_LEN;
    add_grain(v->overlap_buf + pos, v->grain, first, gain);
    add_grain(v->overlap_buf, v->grain + first, GRAIN_LEN - first, gain);
}

static int32_t throat_pulse(mk_voice_t *v, uint32_t period)
{
    if (S.growl == 0) {
        v->noise_period = 0;
        return EXCITATION_SCALE;
    }

    // Noise pulses use the same resonant grain as the regular throat pulses.
    // Random intervals and polarities remove the periodic buzz at full Growl.
    v->noise_rng = v->noise_rng * 1664525u + 1013904223u;
    const uint32_t random = v->noise_rng;
    const uint32_t interval = (period * (48u + (random >> 24))) >> 8;
    v->noise_period = interval < 32u ? 32u : interval;
    const int32_t magnitude = 8192 + ((random >> 8) & 8191);
    const int32_t noise = random & (1u << 23) ? -magnitude : magnitude;
    return EXCITATION_SCALE + (((noise - EXCITATION_SCALE) * S.growl) >> EXCITATION_BITS);
}

static void voice_init(mk_voice_t *v, int i)
{
    int32_t *ob = v->overlap_buf;
    uint32_t k;
    for (k = 0; k < OLAP; k++)
        ob[k] = 0;
    v->active = 0;
    v->current_pitch = v->target_pitch = 57.0f;   /* 220 Hz */
    v->pitch_bend_offset = 0.0f;
    v->glide_param = v->min_glide = 0.0f;
    v->pitch_ramp_ticks = v->vowel_ramp_ticks = 0;
    v->random_jitter = 4.0f;
    v->jitter_counter = 0;
    v->jitter_period = 2293;
    v->rng_state = 12345u + (uint32_t)i * 7919u;  /* staggered: unison voices are separate singers */
    v->noise_rng = 742933u + (uint32_t)i * 12409u;
    v->noise_period = 0;
    v->vibrato_phase = (UINT32_MAX / MAX_UNISON) * (uint32_t)i;
    v->vibrato_depth = 0.0f;
    v->vibrato_rate = 0.5f;
    v->rate_scale = 1.0f;
    v->ramp_period = (int)(MK_FS * 0.01f);
    v->ramp_counter = v->ramp_period;
    v->current_vowel = v->target_vowel = 0.5f;
    v->current_voice = 0.5f;
    v->aspiration_amp = 0.5f;
    v->grain_dirty = 1;
    v->g_vowel = -1.0f;
    v->overlap_write_pos = v->overlap_read_pos = v->overlap_offset = 0;
    v->env_stage = ENV_IDLE;
    v->env_level = 0;
    v->env_attack = v->env_decay = v->env_release = 0.0f;
    v->env_sustain = ENV_SCALE;
}

static int has_env(const mk_voice_t *v)
{
    return v->env_attack > 0.0f || v->env_decay > 0.0f || v->env_sustain < ENV_SCALE || v->env_release > 0.0f;
}

static void voice_note_on(mk_voice_t *v, float note)
{
    uint8_t was = v->active;
    v->active = 1;
    v->target_pitch = note;
    if (!was) {
        v->current_pitch = note;
        v->current_vowel = v->target_vowel;   /* start on the vowel, not where the last note left it */
        v->vowel_ramp_ticks = 0;
        v->grain_dirty = 1;
        v->overlap_write_pos = v->overlap_read_pos;   /* the buffer drains by itself: no pop */
        v->overlap_offset = 0;
        v->noise_period = 0;
    }
    if (has_env(v)) {
        v->env_stage = ENV_ATTACK;            /* from where it is: no click on a retrigger */
    } else {
        v->env_level = ENV_SCALE;
        v->env_stage = ENV_SUSTAIN;
    }
}

static void voice_note_off(mk_voice_t *v)
{
    if (has_env(v)) {
        v->env_stage = ENV_RELEASE;
    } else {
        v->active = 0;
        v->env_stage = ENV_IDLE;
        v->env_level = 0;
    }
}

static int voice_is_active(const mk_voice_t *v) { return v->active || v->env_stage == ENV_RELEASE; }

static void voice_set_pitch_direct(mk_voice_t *v, float note) { v->target_pitch = v->current_pitch = note; }

static void voice_set_vowel(mk_voice_t *v, float vowel)
{
    float t = clamp01(vowel), eff = v->glide_param > v->min_glide ? v->glide_param : v->min_glide;
    if (eff < 0.001f) {
        v->current_vowel = t;
        v->vowel_ramp_ticks = 0;
    } else {
        int n = RAMP_TICKS;
        if (eff > 0.5f)
            n = RAMP_TICKS + (int)((eff - 0.5f) * 180.0f);
        v->vowel_step = (t - v->current_vowel) / (float)n;
        v->vowel_ramp_ticks = n;
    }
    v->target_vowel = t;
}

static float voice_amplitude(const mk_voice_t *v)   /* upstream's pitch compensation */
{
    return clampf((v->current_pitch - 12.0f) * (-1.0f / 72.0f) + 2.0f, 0.1f, 3.0f);
}

static void voice_process(mk_voice_t *v, int32_t *out, uint32_t n)
{
    int env = has_env(v) || v->env_stage == ENV_RELEASE;
    int gliding = v->min_glide > 0.001f || v->current_pitch != v->target_pitch;
    uint32_t i;
    int32_t pitch = base_pitch(v);
    if (!gliding)
        v->current_vowel = v->target_vowel;
    for (i = 0; i < n; i++) {
        if (v->active || v->env_stage == ENV_RELEASE) {
            if (gliding) {
                apply_portamento(v);
                pitch = base_pitch(v);
            }
            const uint32_t period = grain_period(pitch + compute_vibrato(v));
            uint32_t spacing = period;
            if (S.growl && v->noise_period) {
                const int32_t delta = (int32_t)v->noise_period - (int32_t)period;
                spacing = (int32_t)period + ((delta * S.growl) >> EXCITATION_BITS);
            }
            if (v->overlap_offset >= spacing || v->grain_dirty) {
                if (v->grain_dirty || v->current_vowel != v->g_vowel || v->current_voice != v->g_voice ||
                    v->aspiration_amp != v->g_asp)
                    compute_grain(v);
                overlap_add(v, throat_pulse(v, period));
                v->overlap_offset = 0;
            }
        }
        v->overlap_offset++;
    }
    for (i = 0; i < n; i++) {
        uint32_t r = v->overlap_read_pos & (OLAP - 1u);
        int32_t s = v->overlap_buf[r];
        v->overlap_buf[r] = 0;
        v->overlap_read_pos = r + 1u;
        if (env)
            s = (int32_t)(((int64_t)s * env_tick(v)) >> 24);
        out[i] += s;
    }
}

/* ------------------------------------------------------------------- delay */
static void delay_taps(void)
{
    float rs = 0.1f + D.rate * 1.9f;
    D.target_l = (int32_t)(fm_minf(6826.5f * rs * 0.5f, (float)(DL_N - 2u)) * DL_FRAC_SCALE);
    D.target_r = (int32_t)(fm_minf(8785.0f * rs * 0.5f, (float)(DL_N - 2u)) * DL_FRAC_SCALE);
}

static void delay_init(void)
{
    uint32_t i;
    for (i = 0; i < DL_N; i++)
        D.l[i] = D.r[i] = 0;
    D.w = 0;
    D.rate = 0.5f;
    D.mix = GAIN_SCALE / 2;
    delay_taps();
    D.cur_l = D.target_l;
    D.cur_r = D.target_r;
    D.tap_l = D.tap_r = 0;
}

static inline int32_t dl_slew(int32_t current, int32_t target)
{
    const int32_t step = (int32_t)(((int64_t)(target - current) * DL_SMOOTH_STEP) / DL_FRAC_SCALE);
    return step ? current + step : target;
}

static inline int32_t dl_read(const int16_t *buf, int32_t pos)
{
    int32_t read = (int32_t)(D.w * DL_FRAC_SCALE) - pos;
    if (read < 0)
        read += (int32_t)(DL_N * DL_FRAC_SCALE);
    uint32_t index = (uint32_t)read >> DL_FRAC_BITS;
    uint32_t next = index + 1u == DL_N ? 0 : index + 1u;
    int32_t frac = read & (DL_FRAC_SCALE - 1u);
    int32_t a = buf[index];
    int32_t delta = (int32_t)buf[next] - a;
    return a * (1 << DL_SAMPLE_SHIFT) + (int32_t)(((int64_t)delta * frac) >> (DL_FRAC_BITS - DL_SAMPLE_SHIFT));
}

static inline int16_t dl_store(int32_t sample)
{
    sample /= 1 << DL_SAMPLE_SHIFT;
    if (sample > 32767)
        return 32767;
    if (sample < -32767)
        return -32767;
    return (int16_t)sample;
}

/* Both half-rate taps keep their own feedback. Interpolate to the full rate. */
static void delay_process(const int32_t *in, int32_t *ol, int32_t *or_, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i += 2u) {
        int32_t x = (in[i] + in[i + 1u]) / 2;
        D.cur_l = dl_slew(D.cur_l, D.target_l);
        D.cur_r = dl_slew(D.cur_r, D.target_r);
        int32_t tl = dl_read(D.l, D.cur_l);
        int32_t tr = dl_read(D.r, D.cur_r);
        D.l[D.w] = dl_store((int32_t)(((int64_t)(tl / 2 + x) * D.mix) >> 16));
        D.r[D.w] = dl_store((int32_t)(((int64_t)(tr / 2 + x) * D.mix) >> 16));
        if (++D.w == DL_N)
            D.w = 0;
        ol[i] = in[i] + (D.tap_l + tl) / 2;
        or_[i] = in[i] + (D.tap_r + tr) / 2;
        ol[i + 1u] = in[i + 1u] + tl;
        or_[i + 1u] = in[i + 1u] + tr;
        D.tap_l = tl;
        D.tap_r = tr;
    }
}

/* ------------------------------------------------------------------- synth */
static float detuned(float base, int i, int n)
{
    if (n <= 1)
        return base;
    return base + S.unison_detune * ((float)i / (float)(n - 1) * 2.0f - 1.0f) * 0.01f;   /* cents */
}

static void apply_voice_spread(void)
{
    int n = S.unison_count, i;
    for (i = 0; i < n; i++) {
        float off = n > 1 ? S.unison_voice_spread * ((float)i / (float)(n - 1) * 2.0f - 1.0f) : 0.0f;
        S.v[i].current_voice = clamp01(S.base_voice + off);
    }
}

static void apply_unison(float base)
{
    int n = S.unison_count, i;
    S.last_base_note = base;
    for (i = 0; i < n; i++)
        voice_note_on(&S.v[i], detuned(base, i, n));
}

static void remove_note(uint8_t note)
{
    uint32_t i, j = 0;
    for (i = 0; i < S.held_count; i++)
        if (S.held[i] != note)
            S.held[j++] = S.held[i];
    S.held_count = (uint8_t)j;
}

static void synth_note_on(uint8_t note)
{
    int i;
    for (i = 0; i < MAX_UNISON; i++)
        S.v[i].min_glide = 0.0f;
    remove_note(note);
    if (S.held_count < MAX_NOTES)
        S.held[S.held_count++] = note;
    apply_unison((float)note);
}

static void synth_note_off(uint8_t note)
{
    int i;
    remove_note(note);
    if (S.held_count) {
        apply_unison((float)S.held[S.held_count - 1u]);
    } else {
        for (i = 0; i < MAX_UNISON; i++)
            voice_note_off(&S.v[i]);
    }
}

static void synth_set_unison(int count)
{
    int old = S.unison_count, i;
    count = count < 1 ? 1 : count > MAX_UNISON ? MAX_UNISON : count;
    S.unison_count = count;
    S.target_gain = fm_rsqrtf((float)count);
    if (old == count || !voice_is_active(&S.v[0]))
        return;
    for (i = count; i < old; i++)                  /* fade the ones let go (the 3 ms anti-click) */
        S.v[i].env_stage = ENV_RELEASE;
    for (i = 0; i < count && i < old; i++)
        voice_set_pitch_direct(&S.v[i], detuned(S.last_base_note, i, count));
    for (i = old; i < count; i++)
        voice_note_on(&S.v[i], detuned(S.last_base_note, i, count));
    apply_voice_spread();
}

static void synth_set_detune(float cents)
{
    int n = S.unison_count, i;
    S.unison_detune = cents;
    if (voice_is_active(&S.v[0]) && n > 1)
        for (i = 0; i < n; i++)
            voice_set_pitch_direct(&S.v[i], detuned(S.last_base_note, i, n));
}

#define ALLV(stmt)                                                                                      \
    do {                                                                                                \
        int _i;                                                                                         \
        for (_i = 0; _i < MAX_UNISON; _i++) {                                                           \
            mk_voice_t *v = &S.v[_i];                                                                   \
            stmt;                                                                                       \
        }                                                                                               \
    } while (0)

/* a parameter into the engine, with the iOS port's curves (RenderContext.swift) */
static void engine_param(int id, float x)
{
    switch (id) {
    case MK_GLIDE: ALLV(v->glide_param = x); break;
    case MK_DELAY: D.mix = (int32_t)(clamp01(x) * GAIN_SCALE); break;
    case MK_HEAD: S.base_voice = x; apply_voice_spread(); break;
    case MK_VIBRATO: ALLV(v->vibrato_depth = clamp01(x); update_vibrato(v)); break;
    case MK_VIBRATE:
        ALLV(v->vibrato_rate = clamp01(x);
             v->rate_scale = fm_exp2f((v->vibrato_rate * 2.0f - 1.0f) * 2.0f);
             update_vibrato(v));
        break;
    case MK_BREATH: ALLV(v->aspiration_amp = clamp01(x)); break;
    case MK_ATTACK:
        ALLV(v->env_attack = x * 5.0f;
             v->attack_step = (int32_t)(ENV_SCALE / (fm_maxf(v->env_attack, ENV_ANTICLICK) * MK_FS) + 0.5f));
        break;
    case MK_DECAY: ALLV(v->env_decay = x * 5.0f; update_decay_step(v)); break;
    case MK_SUSTAIN: ALLV(v->env_sustain = (int32_t)(clamp01(x) * ENV_SCALE); update_decay_step(v)); break;
    case MK_RELEASE:
        ALLV(v->env_release = x * 5.0f;
             v->release_step = (int32_t)(ENV_SCALE / (fm_maxf(v->env_release, ENV_ANTICLICK) * MK_FS) + 0.5f));
        break;
    case MK_VOICES: synth_set_unison((int)(x * 9.0f + 1.5f)); break;
    case MK_DETUNE: synth_set_detune(x * 50.0f); break;
    case MK_DELAYRATE: D.rate = clamp01(x); delay_taps(); break;
    case MK_LEVEL: S.level = x; break;
    case MK_SPREAD: S.unison_voice_spread = x * 0.5f; apply_voice_spread(); break;
    default: break;
    }
}

/* ------------------------------------------------- the wrapper: queue, slew, pressure, velocity */
enum { C_ON, C_OFF, C_PARAM, C_PRESS, C_TOUCH, C_PANIC };
typedef struct {
    uint8_t type, a, b;
    float f, g;
} mk_cmd_t;
#define NCMD 64u
static mk_cmd_t cmdq[NCMD];
static volatile uint32_t cmd_w, cmd_r;

static struct {
    float p[MK_NPARAM], cur[MK_NPARAM];       /* the knobs' targets and the slewed values */
    float expr, expr_cur;                     /* pressure */
    int route;
    float depth, vel_sens;
    float vel_gain, vel_target;
    float bend_cur;
    uint8_t notes_held;
    float vowel_slew;
} W;

#define SLEW_COEF 0.15f                       /* per 128-sample block: ~35 ms */
#define SLEW_SNAP 0.002f
#define BEND_RANGE 2.0f
static const uint8_t SMOOTH[MK_NPARAM] = {     /* knobs slewed rather than stepped (Schwung's choice) */
    [MK_VOWEL] = 1, [MK_DELAY] = 1, [MK_HEAD] = 1, [MK_VIBRATO] = 1, [MK_VIBRATE] = 1, [MK_BREATH] = 1,
    [MK_DETUNE] = 1, [MK_LEVEL] = 1, [MK_SPREAD] = 1, [MK_TUNE] = 1,
};

static void push(uint8_t type, uint8_t a, uint8_t b, float f, float g)
{
    uint32_t w = cmd_w, nx = (w + 1u) % NCMD;
    if (nx == cmd_r)
        return;                               /* full: dropped (64 deep; the UI sends a few a frame) */
    cmdq[w].type = type;
    cmdq[w].a = a;
    cmdq[w].b = b;
    cmdq[w].f = f;
    cmdq[w].g = g;
    __asm__ volatile("" ::: "memory");
    cmd_w = nx;
}

void monk_note_on(int note, int vel)
{
    if (note >= 0 && note < 128)
        push(C_ON, (uint8_t)note, (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel), 0.0f, 0.0f);
}
void monk_note_off(int note)
{
    if (note >= 0 && note < 128)
        push(C_OFF, (uint8_t)note, 0, 0.0f, 0.0f);
}
void monk_param(int id, float v)
{
    if (id >= 0 && id < MK_NPARAM)
        push(C_PARAM, (uint8_t)id, 0, clamp01(v), 0.0f);
}
void monk_pressure(float p) { push(C_PRESS, 0, 0, clamp01(p), 0.0f); }
void monk_touch(int route, float depth, float vel_sens)
{
    push(C_TOUCH, (uint8_t)(route < 0 || route >= MK_NAT ? 0 : route), 0, clamp01(depth), clamp01(vel_sens));
}
void monk_panic(void) { push(C_PANIC, 0, 0, 0.0f, 0.0f); }

static float vel_gain_of(int vel)
{
    float x = (float)vel * (1.0f / 127.0f);
    return (1.0f - W.vel_sens) + W.vel_sens * x * x * 1.6f;   /* 1.6: a medium press (~100) keeps the level */
}

static float combined_vowel(void)
{
    float d = W.expr_cur * W.depth, v = W.cur[MK_VOWEL];
    switch (W.route) {
    case MK_AT_BOTH_INV: return clamp01(v - d);
    case MK_AT_PITCH: return v;
    default: return clamp01(v + d);
    }
}

float monk_tune_semitones(float normalized)
{
    const float offset = normalized - 0.5f;
    return offset * (offset < 0.0f ? 48.0f : 24.0f);
}

static void apply_bend(void)
{
    const float pressure = W.route == MK_AT_VOWEL ? 0.0f : W.expr_cur * BEND_RANGE;
    const float b = pressure + monk_tune_semitones(W.cur[MK_TUNE]);
    if (b != W.bend_cur) {
        W.bend_cur = b;
        ALLV(v->pitch_bend_offset = clampf(b, -24.0f, 12.0f));
    }
}

static void drain(void)
{
    while (cmd_r != cmd_w) {
        mk_cmd_t c = cmdq[cmd_r];
        cmd_r = (cmd_r + 1u) % NCMD;
        switch (c.type) {
        case C_ON:
            W.vel_target = vel_gain_of(c.b);
            if (!S.held_count)
                W.vel_gain = W.vel_target;         /* a new phrase starts at its own level */
            synth_note_on(c.a);
            break;
        case C_OFF:
            synth_note_off(c.a);
            break;
        case C_PARAM:
            W.p[c.a] = c.f;
            if (!SMOOTH[c.a]) {
                W.cur[c.a] = c.f;
                engine_param(c.a, c.f);
            }
            break;
        case C_PRESS:
            W.expr = c.f;
            break;
        case C_TOUCH:
            W.route = c.a;
            W.depth = c.f;
            W.vel_sens = c.g;
            break;
        case C_PANIC:
            S.held_count = 0;
            ALLV(v->active = 0; v->env_stage = ENV_IDLE; v->env_level = 0);
            W.expr = W.expr_cur = 0.0f;
            break;
        default:
            break;
        }
    }
}

void monk_growl(uint16_t amount)
{
    S.growl = amount > 32768u ? EXCITATION_SCALE : amount / 2;
}

void monk_vibrato_boost(uint16_t amount)
{
    const float depth = (amount > 32768u ? 32768u : amount) * (2.8f / 32768.0f);
    if (vibrato_boost == depth)
        return;
    vibrato_boost = depth;
    ALLV(update_vibrato(v));
}

void monk_vowel_slew(float coefficient)
{
    W.vowel_slew = clamp01(coefficient);
}

static void slew_block(void)
{
    int i;
    float de = W.expr - W.expr_cur;
    for (i = 0; i < MK_NPARAM; i++) {
        float d;
        if (!SMOOTH[i])
            continue;
        d = W.p[i] - W.cur[i];
        if (d == 0.0f)
            continue;
        const float slew = i == MK_VOWEL ? W.vowel_slew : SLEW_COEF;
        W.cur[i] = (d > -SLEW_SNAP && d < SLEW_SNAP) ? W.p[i] : W.cur[i] + d * slew;
        if (i != MK_VOWEL && i != MK_TUNE)
            engine_param(i, W.cur[i]);
    }
    W.expr_cur = (de > -SLEW_SNAP && de < SLEW_SNAP) ? W.expr : W.expr_cur + de * SLEW_COEF;
    {
        float vw = combined_vowel();
        if (vw != S.v[0].target_vowel)
            ALLV(voice_set_vowel(v, vw));
    }
    apply_bend();
    W.vel_gain += (W.vel_target - W.vel_gain) * 0.2f;
}

void monk_init(void)
{
    vibrato_boost = 0.0f;
    static const float DEF[MK_NPARAM] = {   /* upstream's defaults (processor.h) */
        0.5f, 0.5f, 0.8f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.5f};
    int i;
    build_tables();
    for (i = 0; i < MAX_UNISON; i++)
        voice_init(&S.v[i], i);
    S.unison_count = 1;
    S.growl = 0;
    S.unison_detune = 0.0f;
    S.cur_gain = S.target_gain = 1.0f;
    S.held_count = 0;
    S.cc_volume = 0.1f;
    S.level = 1.0f;
    S.base_voice = 0.5f;
    delay_init();
    cmd_w = cmd_r = 0;
    W.route = MK_AT_VOWEL;
    W.depth = 0.5f;
    W.vel_sens = 0.5f;
    W.vel_gain = W.vel_target = 1.0f;
    W.expr = W.expr_cur = 0.0f;
    W.bend_cur = 1e9f;
    W.vowel_slew = SLEW_COEF;
    for (i = 0; i < MK_NPARAM; i++) {
        W.p[i] = W.cur[i] = DEF[i];
        engine_param(i, DEF[i]);
    }
    ALLV(voice_set_vowel(v, 0.5f));
    apply_bend();
}

static int32_t mono[BLOCK], wl[BLOCK], wr[BLOCK];

static int32_t clip_sample(int32_t sample, int32_t gain, int32_t scale)
{
    const int32_t magnitude = sample < 0 ? -sample : sample;
    // Q14 input times Q16 gain to the lookup table's Q15 argument.
    const uint32_t position = (uint32_t)(((int64_t)magnitude * gain) >> 15);
    int32_t shaped = clip_tbl[CLIP_STEPS];
    const uint32_t index = position >> CLIP_FRAC_BITS;
    if (index < CLIP_STEPS) {
        const int32_t frac = position & ((1u << CLIP_FRAC_BITS) - 1u);
        shaped = clip_tbl[index] +
                 (((clip_tbl[index + 1] - clip_tbl[index]) * frac) >> CLIP_FRAC_BITS);
    }
    const int32_t result = (int32_t)(((int64_t)shaped * scale) >> 15);
    return sample < 0 ? -result : result;
}

#if defined(ARDUINO_ARCH_RP2040)
__attribute__((section(".time_critical.monk_render")))
#endif
void monk_render(int32_t *out, uint32_t frames, uint32_t master_q12)
{
    const int32_t scale = (int32_t)(((int64_t)master_q12 * 8300000) >> 12);
    uint32_t i, n = frames > BLOCK ? BLOCK : frames;
    int k;
    drain();
    if (n == 0)
        return;
    slew_block();
    for (i = 0; i < n; i++)
        mono[i] = 0;
    for (k = 0; k < MAX_UNISON; k++) {
        if (k >= S.unison_count && !voice_is_active(&S.v[k]))
            continue;
        voice_process(&S.v[k], mono, n);
    }
    int32_t mix_gain = (int32_t)(S.cur_gain * GAIN_SCALE);
    int32_t gain_step = ((int32_t)(S.target_gain * GAIN_SCALE) - mix_gain) / (int32_t)n;
    for (i = 0; i < n; i++) {
        mono[i] = (int32_t)(((int64_t)mono[i] * mix_gain) >> 16);
        mix_gain += gain_step;
    }
    S.cur_gain = S.target_gain;
    delay_process(mono, wl, wr, n);
    int32_t gain = (int32_t)(voice_amplitude(&S.v[0]) * S.cc_volume * S.level * W.vel_gain * GAIN_SCALE);
    // Bypass the delay output at zero even while old echoes drain from its buffer.
    const int delay_enabled = W.p[MK_DELAY] > 0.0f;
    const int32_t *left = delay_enabled ? wl : mono;
    const int32_t *right = delay_enabled ? wr : mono;
    for (i = 0; i < n; i++) {
        out[2u * i] = clip_sample(left[i], gain, scale);
        out[2u * i + 1u] = clip_sample(right[i], gain, scale);
    }
    monk_ui_vowel = S.v[0].current_vowel;
    monk_ui_amp = clamp01(voice_amplitude(&S.v[0]));
    monk_ui_active = S.held_count != 0;
}
