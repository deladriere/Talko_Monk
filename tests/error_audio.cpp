// Exercise the firmware's actual control and audio-rendering path on the host.
// Only the Pico hardware calls are stubbed. Optional argument: output WAV path.
#include "../src/audio.cpp"
#include "../src/vowel_labels.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <set>
#include <vector>
#include <cstring>

void checkVowels() {
  VowelLabel label;
  const char *expected[] = {"UW", "OH", "AA", "EH", "IY"};
  for (unsigned i = 0; i < 5; ++i) {
    assert(std::strcmp(label.update({uint16_t(i * ControlMax / 4), true}), expected[i]) == 0);
  }
  for (int i = 4; i >= 0; --i) {
    assert(std::strcmp(label.update({uint16_t(i * ControlMax / 4), true}), expected[i]) == 0);
  }
  assert(std::strcmp(label.update({0, false}), "-") == 0);
  assert(std::strcmp(label.update({0, true}), "UW") == 0);
  for (unsigned i = 0; i < 100; ++i) {
    assert(std::strcmp(label.update({uint16_t(511 + i % 3), true}), "UW") == 0);
  }
  assert(std::strcmp(label.update({650, true}), "OH") == 0);
  std::puts("Vowel labels: U/O/A/E/I order, silence and boundary hysteresis passed.");
}

void writeWav(const char *path, const std::vector<int16_t> &samples) {
  std::ofstream file(path, std::ios::binary);
  auto number = [&](uint32_t value, unsigned bytes) {
    while (bytes--) { file.put(char(value & 255)); value >>= 8; }
  };
  file.write("RIFF", 4); number(36 + samples.size() * 2, 4);
  file.write("WAVEfmt ", 8); number(16, 4); number(1, 2); number(1, 2);
  number(FxSampleRate, 4); number(FxSampleRate * 2, 4); number(2, 2); number(16, 2);
  file.write("data", 4); number(samples.size() * 2, 4);
  for (int16_t sample : samples) number(uint16_t(sample), 2);
  assert(file.good());
}

void checkControls() {
  ErrorPerformance performance;
  PanelControls panel{};
  panel.bank = 7;
  panel.effect = Fx::Phaser;
  panel.vowel = 1200; panel.pitch = 900; panel.fx = 2100; panel.speed = 1800;
  panel.error = true;
  for (unsigned i = 0; i < 100; ++i) {
    const auto voice = performance.update(panel, BlockFrames, false);
    assert(!voice.gate && !voice.error); // Arming alone must remain silent.
  }
  panel.gate = true;
  std::set<unsigned> vowels, pitches, effects;
  unsigned live = 0, captured = 0;
  for (unsigned i = 0; i < 5000; ++i) {
    if (i == 5) panel.gate = false;
    const auto voice = performance.update(panel, BlockFrames, false);
    assert(voice.gate && voice.bank == panel.bank && voice.fx == panel.fx);
    assert(voice.vowel <= ControlMax && voice.pitch <= ControlMax && voice.speed <= ControlMax);
    vowels.insert(voice.vowel); pitches.insert(voice.pitch); effects.insert(unsigned(voice.effect));
    voice.error ? ++captured : ++live;
  }
  assert(vowels.size() > 100 && pitches.size() > 12 && effects.size() == 3);
  assert(captured > 0 && live > 3 * captured); // Most of ERROR is the moving live voice.
  panel.error = false;
  const auto restored = performance.update(panel, BlockFrames, false);
  assert(!restored.gate && !restored.error && restored.effect == panel.effect);
  assert(restored.vowel == panel.vowel && restored.pitch == panel.pitch && restored.speed == panel.speed);
  // Endpoint knob positions, rapid presses and live bank changes stay bounded.
  for (unsigned i = 0; i < 20000; ++i) {
    panel.error = i % 7 != 0; panel.gate = true;
    panel.bank = i % PresetCount;
    panel.vowel = i % 2 ? 0 : ControlMax;
    panel.pitch = i % 3 ? 0 : ControlMax;
    panel.speed = i % 5 ? 0 : ControlMax;
    const auto voice = performance.update(panel, BlockFrames, true);
    assert(voice.bank == panel.bank && voice.vowel <= ControlMax && voice.pitch <= ControlMax);
  }
  std::puts("ERROR controls: armed silence, held note, varied live movement, release and endpoints passed.");
}

int main(int argc, char **argv) {
  checkVowels();
  checkControls();
  PanelControls panel{};
  panel.pitch = 1200; panel.vowel = 1350; panel.fx = 2800; panel.speed = 1800;
  panel.effect = Fx::Shimmer;
  setupAudio(panel); setup1();
  std::vector<int16_t> preview;
  int peak = 0;
  for (unsigned bank = 0; bank < PresetCount; ++bank) {
    panel.bank = bank;
    const unsigned blocks = argc > 1 ? (bank == 0 ? 1900 : 0) : 1900;
    for (unsigned block = 0; block < blocks; ++block) {
      panel.gate = block >= 100 && block < 1100;
      panel.error = block >= 280 && block < 1600;
      if (argc == 1) {
        panel.effect = Fx((block / 200) % unsigned(Fx::Count));
        panel.fx = block % 300 < 20 ? 0 : ControlMax;
        panel.speed = block % 401 * ControlMax / 400;
        panel.pitch = block % 751 * ControlMax / 750;
        panel.vowel = block % 361 * ControlMax / 360;
      }
      const uint32_t first = writeIndex.load();
      updateAudioControls(panel); loop1();
      assert(audioVisualState().vowel <= ControlMax);
      if (bank == 0 && block < 90) assert(!audioVisualState().audible);
      assert(writeIndex.load() == first + BlockFrames);
      for (unsigned frame = 0; frame < BlockFrames; ++frame) {
        const unsigned duty = duties[(first + frame) & RingMask];
        assert(duty <= pwmPeriod);
        const int sample = int(duty * 65536 / pwmPeriod) - 32768;
        peak = std::max(peak, std::abs(sample));
        if (argc > 1) preview.push_back(int16_t(sample));
      }
      readIndex.store(writeIndex.load());
    }
  }
  assert(peak > 1000);
  if (argc > 1) writeWav(argv[1], preview);
  std::printf("Audio: all rendered blocks completed; peak=%d.\n", peak);
}
