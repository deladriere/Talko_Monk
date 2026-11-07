// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "audio.h"
#include <monk.h>

// Move the live singer, as the original audio tests did. Short captures are
// accents within the performance; they do not replace it with one frozen vowel.
class ErrorPerformance {
public:
  PanelControls update(const PanelControls &panel, unsigned frames, bool voiceActive) {
    clock += frames;
    if (!panel.error) {
      held = engaged = false;
      return panel;
    }
    if (!held) {
      random ^= clock + (uint32_t(panel.vowel) << 16) + panel.pitch + panel.bank;
      held = true;
      elapsed = duration = 0;
      kind = KindCount;
    }

    PanelControls voice = panel;
    voice.error = false;
    // ERROR can be armed before a note, and keeps that note alive until release.
    engaged = engaged || panel.gate || voiceActive;
    if (!engaged) {
      return voice;
    }
    voice.gate = true;

    if (elapsed >= duration) {
      elapsed = 0;
      const unsigned previous = kind;
      kind = nextRandom() % (previous == KindCount ? KindCount : KindCount - 1);
      if (previous != KindCount && kind >= previous) {
        ++kind;
      }
      const unsigned baseMs = 600 - unsigned(panel.speed) * 450 / ControlMax;
      duration = FxSampleRate * (baseMs * (60 + nextRandom() % 81) / 100) / 1000;
      pitchAccent = Intervals[nextRandom() % IntervalCount];
      vowelAccent = int(nextRandom() % 3073) - 1536;
    }

    const int progress = int(uint64_t(elapsed) * ControlMax / duration);
    int vowel = 0;
    int semitones = 0;
    switch (kind) {
    case 0: { // Fast, articulated vowel chatter.
      const int syllable = progress * 3 / (ControlMax + 1);
      vowel = (progress * 3 % (ControlMax + 1)) - ControlMax / 2;
      semitones = syllable == 1 ? pitchAccent : 0;
      break;
    }
    case 1: // A rising phrase, rather than periodic tape vibrato.
      vowel = progress - ControlMax / 2;
      semitones = -7 + progress * 19 / ControlMax;
      break;
    case 2:
      vowel = ControlMax / 2 - progress;
      semitones = 12 - progress * 24 / ControlMax;
      break;
    case 3: { // Short ratchets alternate with fresh live audio.
      const unsigned pulse = unsigned(progress) * 5 % (ControlMax + 1);
      voice.error = pulse < (ControlMax + 1) * 3 / 5;
      voice.speed = 3000 + unsigned(panel.speed) * (ControlMax - 3000) / ControlMax;
      vowel = vowelAccent;
      semitones = pitchAccent;
      break;
    }
    case 4:
      voice.effect = Fx::Growl;
      vowel = progress < ControlMax / 2 ? progress * 2 - ControlMax / 2
                                            : ControlMax * 3 / 2 - progress * 2;
      semitones = progress < ControlMax / 2 ? 0 : pitchAccent;
      break;
    case 5:
      voice.effect = Fx::Shimmer;
      vowel = (progress * 2 % (ControlMax + 1)) - ControlMax / 2;
      semitones = progress < ControlMax / 3 ? 0 : progress < ControlMax * 2 / 3 ? 7 : 12;
      break;
    }

    // Knobs remain absolute, live controls. Reflect the vowel at its endpoints
    // so either end of SOUND still produces a full, moving mouth shape.
    int mouth = int(panel.vowel) + vowel;
    if (mouth < 0) mouth = -mouth;
    if (mouth > ControlMax) mouth = 2 * ControlMax - mouth;
    voice.vowel = uint16_t(mouth);
    const float base = monk_tune_semitones(panel.pitch * (1.0f / ControlMax));
    const float tune = base + semitones;
    const float knob = 0.5f + tune / (tune < 0.0f ? 48.0f : 24.0f);
    voice.pitch = knob <= 0.0f ? 0 : knob >= 1.0f ? ControlMax : uint16_t(knob * ControlMax);
    elapsed += frames;
    return voice;
  }

private:
  static constexpr unsigned KindCount = 6;
  static constexpr int Intervals[] = {-12, -7, -5, 5, 7, 12};
  static constexpr unsigned IntervalCount = sizeof(Intervals) / sizeof(Intervals[0]);
  uint32_t random = 0x9e3779b9u;
  uint32_t clock = 0;
  unsigned elapsed = 0, duration = 0, kind = KindCount;
  int pitchAccent = 0, vowelAccent = 0;
  bool held = false, engaged = false;

  uint32_t nextRandom() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    if (!random) random = 0x9e3779b9u;
    return random;
  }
};
