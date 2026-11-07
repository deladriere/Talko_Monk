// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "audio.h"

// MONK's formants run U, O, A, E, I. Existing Console S.A.M. labels give
// those vowels their matching mouth shapes without a Console modification.
class VowelLabel {
public:
  const char *update(AudioVisualState voice) {
    if (!voice.audible) {
      selected = -1;
      return "-";
    }
    constexpr int Hysteresis = 80;
    if (selected < 0) {
      selected = (unsigned(voice.vowel) * 4 + ControlMax / 2) / ControlMax;
    }
    while (selected < 4 && int(voice.vowel) > (2 * selected + 1) * ControlMax / 8 + Hysteresis) {
      ++selected;
    }
    while (selected > 0 && int(voice.vowel) < (2 * selected - 1) * ControlMax / 8 - Hysteresis) {
      --selected;
    }
    constexpr const char *Labels[] = {"UW", "OH", "AA", "EH", "IY"};
    return Labels[selected];
  }

private:
  int selected = -1;
};
