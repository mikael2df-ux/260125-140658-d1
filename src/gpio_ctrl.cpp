#include "gpio_ctrl.h"
#include "config.h"
#include "pc_manager.h"

void gpioSetupPin(int8_t pin) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, GPIO_IDLE_LEVEL);
}

void gpioPulse(int8_t pin, uint32_t ms) {
    if (pin < 0) return;
    digitalWrite(pin, GPIO_ACTIVE_LEVEL);
    // Длинные импульсы (>1000ms) делаем делим на куски чтобы не застрять
    uint32_t start = millis();
    while (millis() - start < ms) {
        delay(50);
        yield();
    }
    digitalWrite(pin, GPIO_IDLE_LEVEL);
}

void gpioInitAll() {
    for (int i = 0; i < pcm.count(); i++) {
        gpioSetupPin(pcm.at(i).powerPin);
        gpioSetupPin(pcm.at(i).resetPin);
    }
}
