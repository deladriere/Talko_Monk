// SPDX-License-Identifier: GPL-3.0-only
#include "effects.h"

#include <cmath>
#include <cstring>

namespace {

constexpr int32_t Unity = 32768;
constexpr uint32_t PhasePerHz = UINT32_MAX / FxSampleRate;

int32_t scale(int32_t sample, int32_t gain) {
  return (sample * gain) / Unity;
}

int16_t limit(int32_t sample) {
  return sample < -32768 ? -32768 : sample > 32767 ? 32767 : int16_t(sample);
}

template <unsigned Length> struct DelayLine {
  int16_t samples[Length];
  unsigned index;

  int32_t read() const { return samples[index]; }

  void write(int32_t sample) {
    samples[index] = limit(sample);
    if (++index == Length) {
      index = 0;
    }
  }
};

template <unsigned Length> struct Comb {
  DelayLine<Length> delay;
  int32_t damped;

  int32_t tick(int32_t input, int32_t feedback) {
    const int32_t tail = delay.read();
    damped += (tail - damped) / 4;
    delay.write(input + scale(damped, feedback));
    return tail;
  }
};

template <unsigned Length> struct Diffuser {
  DelayLine<Length> delay;

  int32_t tick(int32_t input) {
    const int32_t delayed = delay.read();
    const int32_t next = limit(input + delayed / 2);
    delay.write(next);
    return delayed - next / 2;
  }
};

constexpr unsigned OctaveBufferFrames = 2048;
unsigned pitchHalfWindow = 400;
unsigned pitchWindowReciprocal = (1u << 20) / 400;
int32_t shimmerDryReduction = 0;

struct Space {
  Comb<653> combA;
  Comb<797> combB;
  Comb<983> combC;
  Comb<1171> combD;
  Diffuser<113> diffuseA;
  Diffuser<47> diffuseB;
  int16_t octaveLine[OctaveBufferFrames];
  uint32_t write;
  unsigned octavePhase, halfWindow, windowReciprocal;
  int32_t octaveIn;
  int32_t shifted;

  int32_t octave(int32_t input) {
    // Two read heads advance twice as fast as the writer. Crossfading the
    // two windows hides the wrap, giving a +1 octave reverb layer.
    constexpr unsigned Mask = OctaveBufferFrames - 1;
    if (octavePhase == 0 && halfWindow != pitchHalfWindow) {
      halfWindow = pitchHalfWindow;
      windowReciprocal = pitchWindowReciprocal;
    }
    const unsigned window = 2 * halfWindow;
    octaveIn += (input - octaveIn) / 2;
    octaveLine[write] = limit(octaveIn);
    const unsigned phase = octavePhase;
    const unsigned other = phase < halfWindow ? phase + halfWindow : phase - halfWindow;
    const unsigned triangle = phase < halfWindow ? phase : window - phase;
    const int32_t weight = (triangle * windowReciprocal) >> 5;
    const int32_t a = octaveLine[(write + phase + 1 - window) & Mask];
    const int32_t b = octaveLine[(write + other + 1 - window) & Mask];
    write = (write + 1) & Mask;
    if (++octavePhase == window) {
      octavePhase = 0;
    }
    return (a * weight + b * (Unity - weight)) / Unity;
  }

  int32_t tick(int32_t input, int32_t amount, bool shimmer) {
    const int32_t feedback = 16384 + scale(amount, shimmer ? 7864 : 12500);
    // Send a quarter of the octave layer back through the damped reverb.
    const int32_t excitation = limit(input + (shimmer ? shifted / 4 : 0));
    const int32_t early = (combA.tick(excitation, feedback) +
                           combB.tick(excitation, feedback) +
                           combC.tick(excitation, feedback) +
                           combD.tick(excitation, feedback)) / 4;
    const int32_t wet = diffuseB.tick(diffuseA.tick(early));
    if (shimmer) {
      // Shift the voice as well as the tail so the octave is audible during the note.
      shifted = octave(limit(input + wet / 2));
      return limit(wet / 2 + shifted * 2);
    }
    return wet;
  }
};

uint32_t phase = 0;
uint32_t phaseStep = 0;
int16_t sine[256];

struct Tape {
  static constexpr unsigned BufferFrames = 4096;
  static constexpr unsigned BufferMask = BufferFrames - 1;
  static constexpr unsigned Fraction = 256;
  static constexpr unsigned MinDelay = 32;
  static constexpr int32_t FullMixAt = Unity / 4;
  static constexpr unsigned FadeFrames = 256;
  static_assert(OctaveBufferFrames + MinDelay + FadeFrames < BufferFrames);

  int16_t samples[BufferFrames];
  unsigned write, filled, delayPhase, halfWindow, windowReciprocal;
  uint32_t wowPhase, flutterPhase;
  int32_t tone, motor, target, mix;

  void setControls(uint16_t knob) {
    mix = knob < FullMixAt ? knob * (Unity / FullMixAt) : Unity;
    // Fade in at half speed, then sweep through unity at noon to double speed.
    const int32_t speedKnob = knob < FullMixAt ? FullMixAt : knob;
    const int32_t centered = speedKnob - Unity / 2;
    const int32_t speedChange = centered > -384 && centered < 384 ? 0 : centered / 64;
    target = -speedChange * int32_t(Fraction);
  }

  int32_t read(unsigned delay) const {
    const unsigned position = write * Fraction - (MinDelay * Fraction + delay);
    const unsigned index = position / Fraction;
    const int32_t fraction = position % Fraction;
    const int32_t first = samples[index & BufferMask];
    const int32_t next = samples[(index + 1) & BufferMask];
    return first + (next - first) * fraction / int32_t(Fraction);
  }

  int16_t tick(int32_t dry) {
    tone += (dry - tone) * 3 / 8;
    samples[write] = limit(tone);
    motor += (target - motor) / 256;
    wowPhase += PhasePerHz * 13 / 10;
    flutterPhase += PhasePerHz * 76 / 10;
    const int32_t wow = sine[wowPhase >> 24] / 2048;
    const int32_t flutter = sine[flutterPhase >> 24] / 8192;
    // Crossfade between two moving heads as their delays wrap.
    if (halfWindow != pitchHalfWindow) {
      halfWindow = pitchHalfWindow;
      windowReciprocal = pitchWindowReciprocal;
      delayPhase %= 2 * halfWindow * Fraction;
    }
    const unsigned headOffset = halfWindow * Fraction;
    const unsigned sweep = 2 * headOffset;
    const unsigned other = delayPhase < headOffset ? delayPhase + headOffset : delayPhase - headOffset;
    const unsigned triangle = delayPhase < headOffset ? delayPhase : sweep - delayPhase;
    const int32_t weight = (triangle * windowReciprocal) >> 13;
    const int32_t wet = (read(delayPhase) * weight + read(other) * (Unity - weight)) / Unity;
    int32_t nextPhase = int32_t(delayPhase) + (motor >> 8) + wow + flutter;
    if (nextPhase < 0) {
      nextPhase += int32_t(sweep);
    } else if (unsigned(nextPhase) >= sweep) {
      nextPhase -= int32_t(sweep);
    }
    delayPhase = nextPhase;
    write = (write + 1) & BufferMask;

    if (filled < OctaveBufferFrames + MinDelay + FadeFrames) {
      ++filled;
    }
    const unsigned warmup = 2 * halfWindow + MinDelay;
    if (mix == 0 || filled <= warmup) {
      return limit(dry);
    }
    const unsigned ready = filled - warmup;
    const int32_t wetMix = ready < FadeFrames ? (mix * int32_t(ready)) / FadeFrames : mix;
    return limit(dry + scale(wet - dry, wetMix));
  }
};

struct TapeFreeze {
  static constexpr unsigned HistoryFrames = 8192;
  static constexpr unsigned HistoryMask = HistoryFrames - 1;
  static constexpr unsigned FadeFrames = 128;
  static constexpr unsigned SpliceFrames = 64;
  static constexpr unsigned QuietFrames = FxSampleRate * 30 / 1000;
  static constexpr int SignalFloor = 48;
  static constexpr unsigned MinFrames = FxSampleRate * 25 / 1000;
  static constexpr unsigned MaxFrames = FxSampleRate * 160 / 1000;
  static constexpr unsigned Unit = FreezeRateUnity;
  static_assert(MaxFrames <= HistoryFrames && MinFrames > 2 * SpliceFrames);

  struct Window {
    unsigned length, offset;
    bool reverse;
  };

  int16_t history[HistoryFrames];
  unsigned head, available, signalFrames;
  unsigned quiet = QuietFrames;
  Window window, nextWindow;
  unsigned position, fade, desired, step;
  uint32_t sweepPhase, sweepStep;
  uint32_t random = 189295u;
  uint16_t initialSound, sound;
  bool held, pending, scanning, splicing;

  void configure(bool pressed, uint16_t speed, uint16_t newSound, uint16_t rate) {
    if (pressed && !held) {
      initialSound = newSound;
      scanning = false;
      pending = true;
    }
    if (pressed) {
      desired = MaxFrames - (MaxFrames - MinFrames) * speed / FxAdcMax;
      sweepStep = PhasePerHz * (100 + 520 * speed / FxAdcMax) / 100;
      sound = newSound;
      const int move = int(sound) - initialSound;
      scanning = scanning || move > 48 || move < -48;
      step = rate;
    } else {
      pending = false;
    }
    held = pressed;
  }

  static int16_t blend(int16_t from, int16_t to, int weight, int full) {
    return from + ((int32_t(to) - from) * weight) / full;
  }

  int16_t read(Window span, unsigned samplePhase) const {
    const unsigned first = head - span.offset - span.length;
    if (span.reverse) {
      samplePhase = span.length * Unit - 1 - samplePhase;
    }
    const unsigned index = samplePhase / Unit;
    const unsigned next = index + 1 < span.length ? index + 1 : index;
    return blend(history[(first + index) & HistoryMask],
                 history[(first + next) & HistoryMask], samplePhase % Unit, Unit);
  }

  Window captureWindow(bool reverse) const {
    const unsigned usable = scanning ? available : signalFrames;
    const unsigned length = desired < usable ? desired : usable;
    const unsigned offset = scanning ? (usable - length) * (FxAdcMax - sound) / FxAdcMax : 0;
    return {length, offset, reverse};
  }

  void start() {
    window = captureWindow(false);
    // Start near the newest audio so a recently started note responds at once.
    position = (window.length / 2) * Unit;
    splicing = false;
    random = random * 1664525u + 1013904223u + uint16_t(history[(head - 1) & HistoryMask]);
    sweepPhase = (random & 0x40000000u) ? 0x40000000u : 0xc0000000u;
    pending = false;
  }

  void record(int16_t live) {
    history[head] = live;
    head = (head + 1) & HistoryMask;
    if (available < HistoryFrames) {
      ++available;
    }
    if (live > SignalFloor || live < -SignalFloor) {
      quiet = 0;
    } else if (quiet < QuietFrames) {
      ++quiet;
    }
    if (quiet == QuietFrames) {
      signalFrames = 0;
    } else if (signalFrames < HistoryFrames) {
      ++signalFrames;
    }
  }

  int16_t tick(int16_t live) {
    if (!window.length) {
      if (pending && signalFrames >= MinFrames) {
        start();
      } else {
        record(live);
        return live;
      }
    }

    int16_t loop = read(window, position);
    const unsigned end = window.length * Unit;
    const unsigned spliceStart = end - SpliceFrames * Unit;
    if (position >= spliceStart) {
      if (!splicing) {
        random = random * 1664525u + 1013904223u;
        nextWindow = captureWindow((random >> 30) == 0);
        splicing = true;
      }
      const unsigned splice = position - spliceStart;
      loop = blend(loop, read(nextWindow, splice), splice, SpliceFrames * Unit);
    }
    const int32_t motor = 240 + sine[sweepPhase >> 24] / 192;
    sweepPhase += sweepStep;
    position += step * motor / Unit;
    if (position >= end) {
      window = nextWindow;
      position = position - end + SpliceFrames * Unit;
      splicing = false;
    }

    // Keep a little live voice underneath the loop so new notes can cut through.
    constexpr unsigned WetFrames = FadeFrames * 7 / 8;
    if (!held || pending) {
      if (fade) {
        --fade;
      }
      if (!fade) {
        window.length = 0;
        available = signalFrames = 0;
        quiet = QuietFrames;
      }
    } else if (fade < WetFrames) {
      ++fade;
    }
    return blend(live, loop, fade, FadeFrames);
  }
};

Tape tape{};
TapeFreeze freeze{};
Space space{};
Fx selected = Fx::Delay;
int32_t wetAmount = 0;
int32_t phaserState[4];

int16_t phaser(int32_t dry) {
  const int32_t coefficient = 17300 + scale(sine[phase >> 24], 12900);
  phase += phaseStep;
  int32_t wet = dry;
  for (int32_t &state : phaserState) {
    const int32_t stage = limit(state - scale(wet, coefficient));
    state = wet + scale(stage, coefficient);
    wet = stage;
  }
  return limit(dry + scale(wet - dry, wetAmount / 2));
}

} // namespace

uint16_t fxAmount(uint16_t adc) {
  if (adc <= FxAdcFloor) {
    return 0;
  }
  return (uint32_t(adc - FxAdcFloor) * Unity) / (FxAdcMax - FxAdcFloor);
}

void setupEffects() {
  constexpr float Tau = 6.28318530718f;
  for (unsigned i = 0; i < 256; ++i) {
    sine[i] = int16_t(std::sin(i * (Tau / 256)) * 32767);
  }
}

void setEffect(Fx mode, uint16_t amount) {
  if (mode != selected) {
    if (mode == Fx::Reverb || mode == Fx::Shimmer) {
      std::memset(&space, 0, sizeof(space));
    }
    if (mode == Fx::Tape) {
      std::memset(&tape, 0, sizeof(tape));
    }
    std::memset(phaserState, 0, sizeof(phaserState));
    phase = 0;
    selected = mode;
  }
  wetAmount = amount;
  if (mode == Fx::Tape) {
    tape.setControls(amount);
  } else if (mode == Fx::Shimmer) {
    shimmerDryReduction = amount / 3;
  } else if (mode == Fx::RingMod) {
    phaseStep = (30u + (uint32_t(amount) * 1170u) / Unity) * PhasePerHz;
  } else if (mode == Fx::Phaser) {
    phaseStep = PhasePerHz * 55 / 100;
  }
}

void setPitchShiftPeriod(uint32_t period) {
  // A whole number of pitch periods between the read heads keeps their
  // octave layers in phase as they crossfade. Aim for about 19 ms between them.
  if (period == 0 || period > OctaveBufferFrames / 2) {
    return;
  }
  const unsigned multiples = (420 + period / 2) / period;
  const unsigned halfWindow = (multiples ? multiples : 1) * period;
  if (pitchHalfWindow != halfWindow) {
    pitchHalfWindow = halfWindow;
    pitchWindowReciprocal = (1u << 20) / halfWindow;
  }
}

// Keep the per-sample paths in SRAM so UI work on the other core does not
// evict their instructions from the shared flash cache.
#if defined(ARDUINO_ARCH_RP2040)
__attribute__((section(".time_critical.monk_effect")))
#endif
int16_t processEffect(int32_t sample) {
  if (wetAmount == 0) {
    // Keep space tails decaying and Tape history current while FX is down.
    if (selected != Fx::Reverb && selected != Fx::Shimmer && selected != Fx::Tape) {
      return limit(sample);
    }
  }
  switch (selected) {
  case Fx::RingMod: {
    const int32_t modulated = scale(sample, sine[phase >> 24]);
    phase += phaseStep;
    return limit(sample + scale(modulated - sample, wetAmount));
  }
  case Fx::Reverb:
  case Fx::Shimmer: {
    const bool shimmer = selected == Fx::Shimmer;
    const int32_t wet = space.tick(sample / 2, wetAmount, shimmer);
    const int32_t dry = shimmer ? sample - scale(sample, shimmerDryReduction) : sample;
    return limit(dry + scale(wet, wetAmount));
  }
  case Fx::Phaser:
    return phaser(sample);
  case Fx::Tape:
    return tape.tick(sample);
  default:
    return limit(sample);
  }
}

void setFreeze(bool held, uint16_t speed, uint16_t sound, uint16_t rate) {
  freeze.configure(held, speed, sound, rate);
}

#if defined(ARDUINO_ARCH_RP2040)
__attribute__((section(".time_critical.monk_freeze")))
#endif
int16_t processFreeze(int16_t sample) {
  return freeze.tick(sample);
}
