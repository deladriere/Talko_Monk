#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
using uint = unsigned;
using std::min;
using std::max;
template <class T> T constrain(T value, T low, T high) {
  return std::max(low, std::min(value, high));
}
inline uint32_t micros() {
  using namespace std::chrono;
  return uint32_t(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}
inline void tight_loop_contents() {}
#define __not_in_flash_func(name) name
#define __isr
constexpr unsigned PICO_HIGHEST_IRQ_PRIORITY = 0;
constexpr unsigned PWM_IRQ_WRAP = 0;
inline void irq_set_exclusive_handler(unsigned, void (*)()) {}
inline void irq_set_priority(unsigned, unsigned) {}
inline void irq_set_enabled(unsigned, bool) {}
