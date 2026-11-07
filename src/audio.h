// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "effects.h"

#include <stdint.h>

struct PanelControls {
  uint16_t vowel;
  uint16_t pitch;
  uint16_t fx;
  uint16_t speed;
  Fx effect = Fx::Delay;
  uint8_t bank;
  uint8_t trigger;
  bool gate;
  bool error;
};

struct AudioStats {
  uint32_t budgetUs;
  uint32_t peakUs;
  uint32_t lateBlocks;
  uint32_t underrunSamples;
};

constexpr uint16_t ControlMax = 4095;

void setupAudio(const PanelControls &controls);
void updateAudioControls(const PanelControls &controls);
AudioStats audioStats();
