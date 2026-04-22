#pragma once
#include <Arduino.h>

// Инициализировать пин в idle-состоянии (если >=0).
void gpioSetupPin(int8_t pin);

// Блокирующий импульс заданной длительности.
// Для Power / Reset кнопок. Использует GPIO_ACTIVE_LEVEL / GPIO_IDLE_LEVEL.
void gpioPulse(int8_t pin, uint32_t ms);

// Инициализация всех пинов, заданных в pcm (powerPin / resetPin)
void gpioInitAll();
