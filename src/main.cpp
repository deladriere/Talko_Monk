// SPDX-License-Identifier: GPL-3.0-only
#include "audio.h"
#include "presets.h"
#include "talko_startup.h"
#include "vowel_labels.h"

#include <Arduino.h>
#include <cstdio>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>

namespace {

constexpr uint ErrorPin = 0;
constexpr uint GatePin = 2;
constexpr uint EncoderA = 3;
constexpr uint EncoderB = 4;
constexpr uint SegmentA = 5;
constexpr uint DecimalPoint = 12;
constexpr uint SelectPin = 13;
constexpr uint ModeB = 14;
constexpr uint ModeA = 15;
constexpr uint8_t VersionMajor = 1;
constexpr uint8_t VersionMinor = 0;
constexpr uint SoundPin = 26;
constexpr uint FxPin = 27;
constexpr uint SpeedPin = 28;
constexpr uint PitchPin = 29;
constexpr uint DisplayBrightness = 100;
constexpr uint32_t IdleMs = 5 * 60 * 1000;
constexpr uint32_t BreathFrameMs = 25;
constexpr uint SoundMax = 4000;
constexpr int PotActivityThreshold = 32;
constexpr uint GateDebounceUs = 1500;
constexpr uint SelectDebounceMs = 10;
constexpr uint EncoderSettleMs = 25;
constexpr uint ModeDebounceMs = 10;
constexpr uint OneShotMs = 500;

enum class PlayMode : uint8_t { OneShot, Gate, Drone };
constexpr const char *ModeNames[] = {"one-shot", "gate", "drone"};

PanelControls controls{};
uint8_t lastEncoderState = 0;
int8_t encoderSteps = 0;
volatile uint8_t bankCursor = 0;
volatile uint8_t fxCursor = unsigned(Fx::Delay);
volatile uint32_t encoderDetents = 0;
volatile uint32_t encoderInvalid = 0;
bool gateRaw = false;
bool gateHeld = false;
uint32_t gateChangedAt = 0;
bool consoleConnected = false;
volatile bool fxPage = false;
PlayMode mode = PlayMode::Gate;
PlayMode rawMode = PlayMode::Gate;
uint32_t modeChangedAt = 0;
uint32_t shotStartedAt = 0;
struct Button {
  uint pin;
  bool raw = false;
  bool held = false;
  uint32_t changedAt = 0;

  bool update(uint32_t now) {
    const bool pressed = !gpio_get(pin);
    if (pressed != raw) {
      raw = pressed;
      changedAt = now;
    }
    if (raw == held || now - changedAt < SelectDebounceMs) {
      return false;
    }
    held = raw;
    return true;
  }
};

Button selectButton{SelectPin};
Button errorButton{ErrorPin};

struct Pot {
  uint pin;
  uint32_t filtered;
  uint16_t lastActivity = 0;

  void begin() {
    lastActivity = analogRead(pin);
    filtered = lastActivity * 4;
  }

  bool update() {
    filtered += analogRead(pin) - int32_t(value());
    const uint16_t current = value();
    if (abs(int(current) - lastActivity) < PotActivityThreshold) {
      return false;
    }
    lastActivity = current;
    return true;
  }

  uint16_t value() const { return filtered / 4; }
};

Pot sound{SoundPin, 0};
Pot pitch{PitchPin, 0};
Pot fxPot{FxPin, 0};
Pot speed{SpeedPin, 0};

uint8_t encoderState() {
  const uint32_t pins = gpio_get_all();
  return (((pins >> EncoderA) & 1u) << 1) | ((pins >> EncoderB) & 1u);
}

void encoderInterrupt() {
  constexpr int8_t transitions[16] = {
       0, -1,  1,  0,
       1,  0,  0, -1,
      -1,  0,  0,  1,
       0,  1, -1,  0,
  };
  const uint8_t state = encoderState();
  const uint8_t previous = lastEncoderState;
  lastEncoderState = state;
  const int8_t step = transitions[(previous << 2) | state];
  if (step == 0) {
    if (state != previous) {
      ++encoderInvalid;
    }
    return;
  }

  encoderSteps += step;
  if (encoderSteps != 4 && encoderSteps != -4) {
    return;
  }

  const int movement = encoderSteps > 0 ? 1 : -1;
  encoderSteps = 0;
  ++encoderDetents;
  // Apply the limit on each click so a reversal at either end responds at once.
  if (fxPage) {
    const int effect = int(fxCursor) + movement;
    constexpr int count = int(Fx::Count);
    fxCursor = (effect + count) % count;
  } else {
    bankCursor = constrain(int(bankCursor) + movement, 0, PresetCount - 1);
  }
}

bool updateGate() {
  // The gate input is active low on the microcontroller.
  const bool raw = !gpio_get(GatePin);
  const uint32_t now = micros();
  if (raw != gateRaw) {
    gateRaw = raw;
    gateChangedAt = now;
  }
  if (raw == gateHeld || now - gateChangedAt < GateDebounceUs) {
    return false;
  }
  gateHeld = raw;
  return true;
}

PlayMode readMode() {
  constexpr uint32_t mask = (1u << ModeA) | (1u << ModeB);
  const uint32_t pins = gpio_get_all() & mask;
  if (pins == (1u << ModeB)) {
    return PlayMode::OneShot;
  }
  if (pins == (1u << ModeA)) {
    return PlayMode::Drone;
  }
  return PlayMode::Gate;
}

bool updateMode(uint32_t now) {
  const PlayMode next = readMode();
  if (next != rawMode) {
    rawMode = next;
    modeChangedAt = now;
  }
  if (next == mode || now - modeChangedAt < ModeDebounceMs) {
    return false;
  }
  mode = next;
  return true;
}

void updateNote(uint32_t now, bool rising, bool modeChanged) {
  if (modeChanged) {
    controls.gate = false;
  }
  switch (mode) {
  case PlayMode::OneShot:
    if (rising) {
      controls.gate = true;
      shotStartedAt = now;
      ++controls.trigger;
    } else if (controls.gate && now - shotStartedAt >= OneShotMs) {
      controls.gate = false;
    }
    break;
  case PlayMode::Gate:
    controls.gate = gateHeld;
    break;
  case PlayMode::Drone:
    if (rising) {
      controls.gate = !controls.gate;
    }
    break;
  }
}

void readPots() {
  controls.vowel = min(uint(sound.value()), SoundMax) * ControlMax / SoundMax;
  controls.pitch = pitch.value();
  controls.fx = fxPot.value();
  controls.speed = speed.value();
}

void drawDisplay(uint8_t segments) {
  for (uint bit = 0; bit < 8; ++bit) {
    pwm_set_gpio_level(SegmentA + bit, (segments & (1u << bit)) ? DisplayBrightness : 0);
  }
}

void showSelection() {
  const uint8_t glyph = fxPage ? FxLabels[unsigned(controls.effect)].glyph
                               : Presets[controls.bank].segments;
  drawDisplay(glyph | (fxPage ? TalkoStartup::Dot : 0));
}

void showSleepingMonk(uint32_t elapsed) {
  constexpr uint32_t InhaleMs = 1100;
  constexpr uint32_t ExhaleMs = 2000;
  constexpr uint32_t CycleMs = 4200;
  constexpr uint8_t Closed = 0x40;
  constexpr uint8_t HalfOpen = 0x5c;
  constexpr uint8_t Open = 0x3f;
  const uint32_t phase = elapsed % CycleMs;

  uint32_t breath = 0;
  if (phase < InhaleMs) {
    breath = phase * 255 / InhaleMs;
  } else if (phase < InhaleMs + ExhaleMs) {
    breath = (InhaleMs + ExhaleMs - phase) * 255 / ExhaleMs;
  }

  // Crossfade the centre line into a small mouth, then into the full oval.
  const uint8_t from = breath < 128 ? Closed : HalfOpen;
  const uint8_t to = breath < 128 ? HalfOpen : Open;
  const uint32_t mix = (breath & 127u) * 2;
  const uint32_t brightness = 9 + 31 * breath / 255;
  for (uint bit = 0; bit < 7; ++bit) {
    const uint8_t segment = 1u << bit;
    const uint32_t level = (from & segment ? 255 - mix : 0) + (to & segment ? mix : 0);
    pwm_set_gpio_level(SegmentA + bit, brightness * level / 255);
  }
  pwm_set_gpio_level(DecimalPoint, 0);
}

void updateDisplay(uint32_t now, bool activity, bool selectionChanged) {
  static uint32_t lastActivity = now;
  static uint32_t lastBreathFrame = 0;
  static bool sleeping = false;
  if (activity) {
    lastActivity = now;
  }

  const uint32_t idle = now - lastActivity;
  if (idle >= IdleMs) {
    const uint32_t frame = (idle - IdleMs) / BreathFrameMs;
    if (!sleeping || frame != lastBreathFrame) {
      showSleepingMonk(frame * BreathFrameMs);
      lastBreathFrame = frame;
    }
    sleeping = true;
  } else if (sleeping || selectionChanged) {
    showSelection();
    sleeping = false;
  }
}

enum ConsoleReport : uint8_t {
  ReportStatus = 1,
  ReportVowel = 2,
};

void serviceConsole() {
  static uint8_t pending = 0;
  static char buffer[512];
  static size_t length = 0;
  static size_t sent = 0;
  static VowelLabel vowelLabel;
  static const char *currentVowel = "-";
  static const char *reportedVowel = nullptr;
  static uint32_t lastVowelCheck = 0, lastVowelReport = 0;
  static bool mouthEnabled = false;
  static unsigned requestOffset = 0;
  constexpr char MouthRequest[] = "\x1eSAM\x1f";
  if (!Serial) {
    consoleConnected = false;
    pending = 0;
    length = sent = 0;
    mouthEnabled = false;
    requestOffset = 0;
    return;
  }

  if (!consoleConnected) {
    pending |= ReportVowel;
  }
  for (unsigned i = 0; i < 16 && Serial.available(); ++i) {
    const int byte = Serial.read();
    if (byte == '?') pending |= ReportStatus;
    if (byte == MouthRequest[requestOffset]) {
      if (++requestOffset == sizeof(MouthRequest) - 1) {
        mouthEnabled = true;
        pending |= ReportVowel;
        requestOffset = 0;
      }
    } else {
      requestOffset = byte == MouthRequest[0] ? 1 : 0;
    }
  }
  consoleConnected = true;

  const uint32_t now = millis();
  if (now - lastVowelCheck >= 25) {
    lastVowelCheck = now;
    currentVowel = vowelLabel.update(audioVisualState());
    if (currentVowel != reportedVowel || now - lastVowelReport >= 200) {
      pending |= ReportVowel;
    }
  }

  if (!pending && sent == length) {
    return;
  }
  const int available = Serial.availableForWrite();
  if (available <= 0) {
    return;
  }
  if (sent == length) {
    sent = length = 0;
    auto append = [&](const char *format, auto... args) {
      const size_t space = sizeof(buffer) - length;
      const int written = snprintf(buffer + length, space, format, args...);
      if (written > 0) {
        length += min(size_t(written), space - 1);
      }
    };

    const bool status = pending & ReportStatus;
    if (status) {
      append("Talko 2 Monk Synth %u.%u\n", unsigned(VersionMajor), unsigned(VersionMinor));
    }
    if (status) {
      const Preset &preset = Presets[controls.bank];
      append("BANK %c %s\n", preset.bank, preset.name);
    }
    if (status) {
      append("EFFECT %s page=%s\n", FxLabels[unsigned(controls.effect)].name, fxPage ? "FX" : "BANK");
    }
    if (status) {
      append("PANEL sound=%u pitch=%u fx=%u speed=%u gate=%u error=%u\n",
             unsigned(controls.vowel), unsigned(controls.pitch), unsigned(controls.fx),
             unsigned(controls.speed), unsigned(gateHeld), unsigned(controls.error));
      append("MODE %s note=%u\n", ModeNames[unsigned(mode)], unsigned(controls.gate));
      append("ENCODER detents=%lu invalid=%lu\n",
             (unsigned long)encoderDetents, (unsigned long)encoderInvalid);
    }
    if (status) {
      const AudioStats stats = audioStats();
      append("AUDIO budget_us=%lu peak_us=%lu late_blocks=%lu underrun_samples=%lu\n",
             (unsigned long)stats.budgetUs, (unsigned long)stats.peakUs,
             (unsigned long)stats.lateBlocks, (unsigned long)stats.underrunSamples);
    }
    if (status || (pending & ReportVowel)) {
      // Plain labels serve text consoles. Framed events hold Console's S.A.M.
      // mouth until the next vowel or silence, even on a sustained drone.
      append("%s\n", currentVowel);
      if (mouthEnabled) append("\x1eSAM:%s\x1f", currentVowel);
      reportedVowel = currentVowel;
      lastVowelReport = now;
    }
    pending = 0;
  }
  // Like SpeakJet, only write bytes the USB transport can take immediately.
  sent += Serial.write(reinterpret_cast<const uint8_t *>(buffer) + sent,
                       min(length - sent, size_t(available)));
}

} // namespace

void setup() {
  TalkoStartup::prepareLed();
  pinMode(GatePin, INPUT_PULLUP);
  pinMode(ErrorPin, INPUT_PULLUP);
  pinMode(EncoderA, INPUT_PULLUP);
  pinMode(EncoderB, INPUT_PULLUP);
  pinMode(SelectPin, INPUT_PULLUP);
  pinMode(ModeA, INPUT_PULLUP);
  pinMode(ModeB, INPUT_PULLUP);
  mode = rawMode = readMode();
  for (uint pin = SegmentA; pin <= DecimalPoint; ++pin) {
    const uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, 255);
    pwm_set_gpio_level(pin, 0);
    gpio_set_function(pin, GPIO_FUNC_PWM);
    pwm_set_enabled(slice, true);
  }

  analogReadResolution(12);
  sound.begin();
  pitch.begin();
  fxPot.begin();
  speed.begin();
  readPots();
  delay(EncoderSettleMs);
  TalkoStartup::showVersion(VersionMajor, VersionMinor, drawDisplay);
  lastEncoderState = encoderState();
  attachInterrupt(digitalPinToInterrupt(EncoderA), encoderInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EncoderB), encoderInterrupt, CHANGE);
  showSelection();

  Serial.begin(115200);
  setupAudio(controls);
}

void loop() {
  const uint32_t now = millis();
  const bool pageChanged = selectButton.update(now) && selectButton.held;
  if (pageChanged) {
    fxPage = !fxPage;
  }
  const bool errorChanged = errorButton.update(now);
  controls.error = errorButton.held;
  const uint8_t bank = bankCursor;
  const Fx effect = Fx(fxCursor);
  const bool bankChanged = bank != controls.bank;
  const bool effectChanged = effect != controls.effect;
  controls.bank = bank;
  controls.effect = effect;
  const bool gateChanged = updateGate();
  const bool modeChanged = updateMode(now);
  const bool noteWasActive = controls.gate;
  const bool rising = gateChanged && gateHeld;
  updateNote(now, rising, modeChanged);
  const bool noteChanged = noteWasActive != controls.gate;
  static uint32_t lastPotRead = 0;
  const bool potsSampled = now - lastPotRead >= 1;
  bool potMoved = false;
  if (potsSampled) {
    lastPotRead = now;
    const bool soundMoved = sound.update();
    const bool pitchMoved = pitch.update();
    const bool fxMoved = fxPot.update();
    const bool speedMoved = speed.update();
    potMoved = soundMoved || pitchMoved || fxMoved || speedMoved;
    readPots();
  }

  static uint32_t lastDetents = 0;
  const uint32_t detents = encoderDetents;
  const bool encoderMoved = detents != lastDetents;
  lastDetents = detents;
  const bool selectionChanged = pageChanged || bankChanged || effectChanged;
  const bool activity = encoderMoved || selectionChanged || selectButton.held ||
                        errorChanged || controls.error || modeChanged || gateChanged ||
                        gateHeld || controls.gate || potMoved;
  updateDisplay(now, activity, selectionChanged);
  if (bankChanged || effectChanged || rising || noteChanged || errorChanged || potsSampled) {
    updateAudioControls(controls);
  }
  serviceConsole();
}
