#pragma once
struct pwm_config {};
constexpr unsigned GPIO_FUNC_PWM = 0;
inline unsigned pwm_gpio_to_slice_num(unsigned) { return 0; }
inline unsigned pwm_gpio_to_channel(unsigned) { return 0; }
inline pwm_config pwm_get_default_config() { return {}; }
inline void pwm_config_set_wrap(pwm_config *, unsigned) {}
inline void pwm_init(unsigned, pwm_config *, bool) {}
inline void pwm_set_chan_level(unsigned, unsigned, unsigned) {}
inline void gpio_set_function(unsigned, unsigned) {}
inline void pwm_clear_irq(unsigned) {}
inline void pwm_set_irq_enabled(unsigned, bool) {}
inline void pwm_set_enabled(unsigned, bool) {}
