#include "GpioHold.h"

#include <driver/gpio.h>

namespace acorn {

void holdOutput(uint8_t pin, uint8_t level) {
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_en(static_cast<gpio_num_t>(pin));
}

void releaseOutput(uint8_t pin, uint8_t level) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  gpio_hold_dis(static_cast<gpio_num_t>(pin));
}

}  // namespace acorn
