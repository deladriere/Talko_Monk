// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <stdint.h>

enum class Fx : uint8_t { Off, Vibrato, Reverb, Delay, RingMod, Shimmer, Phaser, Growl, Tape, Count };

struct FxLabel {
  const char *name;
  uint8_t glyph;
};

constexpr FxLabel FxLabels[] = {
    {"Off", 0x40},      // -
    {"Vibrato", 0x1c},  // v
    {"Reverb", 0x50},   // r
    {"Delay", 0x5e},    // d
    {"Ring mod", 0x5c}, // o
    {"Shimmer", 0x6d},  // S
    {"Phaser", 0x73},   // P
    {"Growl", 0x7d},    // G
    {"Tape", 0x78},     // t
};
static_assert(sizeof(FxLabels) / sizeof(FxLabels[0]) == unsigned(Fx::Count));

constexpr uint16_t FxAdcMax = 4095;
constexpr uint16_t FxAdcFloor = 48;
constexpr uint32_t FxSampleRate = 22050;

uint16_t fxAmount(uint16_t adc); // Q15 wet amount
void setupEffects();
void setEffect(Fx mode, uint16_t amount);
void setPitchShiftPeriod(uint32_t period);
int16_t processEffect(int32_t sample);

constexpr uint16_t FreezeRateUnity = 256;
void setFreeze(bool held, uint16_t speed, uint16_t sound, uint16_t rate);
int16_t processFreeze(int16_t sample);
