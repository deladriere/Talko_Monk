#pragma once

#include <Arduino.h>
#include "ws2812.pio.h"

namespace TalkoStartup {

constexpr uint LedPin = 16;
constexpr uint LedStateMachine = 0;
constexpr uint8_t Dot = 0x80;
constexpr uint8_t Point = 10;
constexpr uint8_t Digits[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
};

inline void prepareLed() {
  pinMode(LedPin, OUTPUT);
  digitalWrite(LedPin, LOW);
}

inline void showVersion(uint8_t major, uint8_t minor, void (*show)(uint8_t),
                        void (*say)(uint8_t) = nullptr, bool (*busy)() = nullptr) {
  pio_sm_claim(pio0, LedStateMachine);
  const uint offset = pio_add_program(pio0, &ws2812_program);
  ws2812_program_init(pio0, LedStateMachine, offset, LedPin, 800000, false);
  pio_sm_put_blocking(pio0, LedStateMachine, 0);
  delay(5);

  auto part = [&](uint8_t glyph, uint8_t spoken, uint32_t grb, uint8_t flashes) {
    show(glyph);
    if (say) {
      say(spoken);
    }
    if (flashes == 0) {
      delay(450);
    }
    for (uint8_t i = 0; i < flashes; ++i) {
      pio_sm_put_blocking(pio0, LedStateMachine, grb << 8);
      delay(150);
      pio_sm_put_blocking(pio0, LedStateMachine, 0);
      delay(300);
    }
    while (busy && busy()) {
      delay(1);
    }
  };

  part(Digits[major], major, 100u << 8, major);
  part(Dot, Point, 255, 1);
  part(Digits[minor], minor, 100u << 16, minor);
}

} // namespace TalkoStartup
