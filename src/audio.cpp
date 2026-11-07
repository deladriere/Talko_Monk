// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include "presets.h"

#include <Arduino.h>
#include <atomic>
#include <cmath>
#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include <pico/critical_section.h>

namespace {

constexpr uint AudioPin = 1;
constexpr uint SampleRate = FxSampleRate;
constexpr uint CarrierCycles = 4;
constexpr uint BlockFrames = 128;
constexpr uint BlockBudgetUs = (BlockFrames * 1000000u + SampleRate / 2) / SampleRate;
constexpr uint RingFrames = BlockFrames * 2;
constexpr uint RingMask = RingFrames - 1;
constexpr uint BaseNote = 57; // A2 in the FM-1 engine
constexpr uint Velocity = 100;
static_assert((RingFrames & RingMask) == 0);
static_assert(SampleRate == MK_FS);

critical_section_t controlsLock;
PanelControls latestControls{};
std::atomic<bool> ready{false};
std::atomic<uint32_t> readIndex{0};
std::atomic<uint32_t> writeIndex{0};
std::atomic<uint32_t> peakUs{0};
std::atomic<uint32_t> lateBlocks{0};
std::atomic<uint32_t> underrunSamples{0};
uint16_t duties[RingFrames];

uint audioSlice;
uint audioChannel;
uint pwmPeriod;
uint carrierPhase = 0;
bool outputStarted = false;

uint __not_in_flash_func(nextDuty)() {
  const uint32_t read = readIndex.load(std::memory_order_relaxed);
  const uint32_t available = writeIndex.load(std::memory_order_acquire) - read;
  if (!outputStarted) {
    outputStarted = available == RingFrames;
    if (!outputStarted) {
      return pwmPeriod / 2;
    }
  }

  if (available == 0) {
    underrunSamples.store(underrunSamples.load(std::memory_order_relaxed) + 1,
                          std::memory_order_relaxed);
    return pwmPeriod / 2;
  }
  const uint duty = duties[read & RingMask];
  readIndex.store(read + 1, std::memory_order_release);
  return duty;
}

void __isr __not_in_flash_func(audioInterrupt)() {
  pwm_clear_irq(audioSlice);
  if (++carrierPhase == CarrierCycles) {
    carrierPhase = 0;
    pwm_set_chan_level(audioSlice, audioChannel, nextDuty());
  }
}

PanelControls readControls() {
  critical_section_enter_blocking(&controlsLock);
  PanelControls controls = latestControls;
  critical_section_exit(&controlsLock);
  return controls;
}

float normalized(uint16_t value) {
  return value * (1.0f / ControlMax);
}

float pitchControl(uint16_t adc) {
  return abs(int(adc) - 2048) <= 56 ? 0.5f : normalized(adc);
}

float vowelSlew(uint16_t adc) {
  // 0.15 is the response of the original firmware, at noon.
  const float travel = normalized(adc) * 2.0f;
  if (travel <= 1.0f) {
    return 0.012f + 0.138f * travel * travel;
  }
  const float fast = travel - 1.0f;
  return 0.15f + 0.85f * fast * fast;
}

void applyControls(const PanelControls &next) {
  static PanelControls previous{};
  static bool firstBlock = true;
  static float freezePitch = 0.0f;
  static uint16_t freezeRate = FreezeRateUnity;
  const bool bankChanged = firstBlock || next.bank != previous.bank;
  const Preset &preset = Presets[next.bank];
  const bool effectChanged = firstBlock || next.effect != previous.effect || next.fx != previous.fx;

  if (bankChanged) {
    for (int param = 0; param < MK_NPARAM; ++param) {
      if (param == MK_VOWEL || param == MK_TUNE || param == MK_DELAY || param == MK_VIBRATO) {
        continue;
      }
      monk_param(param, preset.parameters[param]);
    }
  }
  if (bankChanged || next.vowel != previous.vowel) {
    monk_param(MK_VOWEL, normalized(next.vowel));
  }
  if (bankChanged || next.pitch != previous.pitch) {
    monk_param(MK_TUNE, pitchControl(next.pitch));
  }
  if (bankChanged || effectChanged) {
    const uint16_t amount = fxAmount(next.fx);
    const float depth = amount * (1.0f / 32768.0f);
    const float presetDepth = preset.parameters[MK_VIBRATO];
    const float vibrato = next.effect == Fx::Vibrato ? presetDepth + (1.0f - presetDepth) * depth
                                                    : presetDepth;
    monk_param(MK_VIBRATO, vibrato);
    // Snider's factory patch is dry, but the Delay page can still give it repeats.
    const float presetDelay = preset.parameters[MK_DELAY];
    const float delay = presetDelay > 0.0f ? presetDelay : 0.65f;
    monk_param(MK_DELAY, next.effect == Fx::Delay ? delay * depth : 0.0f);
    monk_growl(next.effect == Fx::Growl ? amount : 0);
    setEffect(next.effect, amount);
  }
  if (next.gate && (!previous.gate || next.trigger != previous.trigger)) {
    monk_note_on(BaseNote, Velocity);
  } else if (!next.gate && previous.gate) {
    monk_note_off(BaseNote);
  }

  if (next.error) {
    if (!previous.error) {
      freezePitch = monk_tune_semitones(pitchControl(next.pitch));
      freezeRate = FreezeRateUnity;
    } else if (next.pitch != previous.pitch) {
      // Relative tuning preserves the sound's pitch at the instant of capture.
      const float bend = monk_tune_semitones(pitchControl(next.pitch)) - freezePitch;
      const float octaves = constrain(bend / 12.0f, -2.0f, 2.0f);
      freezeRate = uint16_t(std::exp2(octaves) * FreezeRateUnity + 0.5f);
    }
  } else if (firstBlock || previous.error || next.speed != previous.speed) {
    monk_vowel_slew(vowelSlew(next.speed));
  }
  setFreeze(next.error, next.speed, next.vowel, freezeRate);
  previous = next;
  firstBlock = false;
}

} // namespace

void setupAudio(const PanelControls &controls) {
  critical_section_init(&controlsLock);
  latestControls = controls;
  audioSlice = pwm_gpio_to_slice_num(AudioPin);
  audioChannel = pwm_gpio_to_channel(AudioPin);
  constexpr uint carrierRate = SampleRate * CarrierCycles;
  pwmPeriod = (clock_get_hz(clk_sys) + carrierRate / 2) / carrierRate;

  pwm_config config = pwm_get_default_config();
  pwm_config_set_wrap(&config, pwmPeriod - 1);
  pwm_init(audioSlice, &config, false);
  pwm_set_chan_level(audioSlice, audioChannel, pwmPeriod / 2);
  gpio_set_function(AudioPin, GPIO_FUNC_PWM);
  pwm_clear_irq(audioSlice);
  irq_set_exclusive_handler(PWM_IRQ_WRAP, audioInterrupt);
  // On the panel core, let PWM updates preempt USB serial work.
  irq_set_priority(PWM_IRQ_WRAP, PICO_HIGHEST_IRQ_PRIORITY);
  pwm_set_irq_enabled(audioSlice, true);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  pwm_set_enabled(audioSlice, true);

  ready.store(true, std::memory_order_release);
}

void updateAudioControls(const PanelControls &controls) {
  critical_section_enter_blocking(&controlsLock);
  latestControls = controls;
  critical_section_exit(&controlsLock);
}

AudioStats audioStats() {
  return {BlockBudgetUs, peakUs.load(std::memory_order_relaxed),
          lateBlocks.load(std::memory_order_relaxed),
          underrunSamples.load(std::memory_order_relaxed)};
}

bool core1_separate_stack = true;

void setup1() {
  while (!ready.load(std::memory_order_acquire)) {
    tight_loop_contents();
  }
  monk_init();
  setupEffects();
}

void loop1() {
  const uint32_t write = writeIndex.load(std::memory_order_relaxed);
  const uint32_t read = readIndex.load(std::memory_order_acquire);
  if (write - read > RingFrames - BlockFrames) {
    tight_loop_contents();
    return;
  }

  const uint32_t start = micros();
  const PanelControls panel = readControls();
  applyControls(panel);
  static int32_t stereo[BlockFrames * 2];
  monk_render(stereo, BlockFrames, 4096);
  if (panel.effect == Fx::Shimmer || panel.effect == Fx::Tape) {
    setPitchShiftPeriod(monk_pitch_period());
  }
  for (uint i = 0; i < BlockFrames; ++i) {
    // Average the stereo channels and convert the 24-bit PCM to PWM duty.
    const int16_t mono = processEffect((stereo[2 * i] + stereo[2 * i + 1]) / 512);
    const int32_t processed = processFreeze(mono);
    duties[(write + i) & RingMask] = ((processed + 32768) * pwmPeriod + 32768u) >> 16;
  }
  writeIndex.store(write + BlockFrames, std::memory_order_release);

  const uint32_t elapsed = micros() - start;
  if (elapsed > peakUs.load(std::memory_order_relaxed)) {
    peakUs.store(elapsed, std::memory_order_relaxed);
  }
  if (elapsed > BlockBudgetUs) {
    lateBlocks.store(lateBlocks.load(std::memory_order_relaxed) + 1,
                     std::memory_order_relaxed);
  }
}
