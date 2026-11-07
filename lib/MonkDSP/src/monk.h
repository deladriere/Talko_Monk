/* SPDX-License-Identifier: MIT AND GPL-3.0-only */
/* MONK-1's sound: MonkSynth (Jonathan Taylor, MIT; github.com/JonET/monksynth), its FOF vocal voice,
 * monophonic note stack, unison and stereo delay, ported to the FM-1 (monk.c). No HAL; the host tests
 * build it too. On Talko, the audio worker calls both the setters and render. Setters take effect
 * at the start of the next block. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MK_FS 22050.0f

/* the parameters, normalized 0..1 as upstream stores them (iOS Params.swift order) */
enum {
    MK_GLIDE, MK_VOWEL, MK_DELAY, MK_HEAD, MK_VIBRATO, MK_VIBRATE, MK_BREATH,
    MK_ATTACK, MK_DECAY, MK_SUSTAIN, MK_RELEASE, MK_VOICES, MK_DETUNE, MK_DELAYRATE,
    MK_LEVEL, MK_SPREAD, MK_TUNE,
    MK_NPARAM
};
/* where pressure goes (the Schwung port's routings: it adds to the Vowel knob, or bends) */
enum { MK_AT_VOWEL, MK_AT_PITCH, MK_AT_BOTH, MK_AT_BOTH_INV, MK_NAT };

void monk_init(void);
void monk_note_on(int note, int vel);          /* MIDI note, velocity 1..127 */
void monk_note_off(int note);
float monk_tune_semitones(float normalized); /* PITCH curve, two octaves down and one up */
uint32_t monk_pitch_period(void);            /* current base period in samples */
void monk_growl(uint16_t amount);             /* Q15 noise amount, on the audio core */
void monk_vowel_slew(float coefficient);     /* per-block interpolation on the audio core */
void monk_param(int id, float v);              /* MK_*, 0..1 */
void monk_pressure(float p);                   /* the sounding key's pressure, 0..1 */
void monk_touch(int route, float depth, float vel_sens);   /* MK_AT_*, how far full pressure moves; velocity 0..1 */
void monk_panic(void);
/* interleaved L/R, 24-bit samples in int32 (Felucca's I2S format: +-8300000 full scale); master 0..4096 (Q12) */
void monk_render(int32_t *out, uint32_t frames, uint32_t master_q12);

/* for the face (written by the render): the vowel being sung, the loudness (upstream's pitch
 * compensation, 0..1 clamped as the iOS view does), whether a note is held */
extern volatile float monk_ui_vowel, monk_ui_amp;
extern volatile uint8_t monk_ui_active;

#ifdef __cplusplus
}
#endif
