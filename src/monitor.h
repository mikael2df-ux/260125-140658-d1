#pragma once
#include <Arduino.h>
#include "pc_manager.h"

// Колбэк: вызывается при смене статуса ПК
// nowOnline = true  -> ПК поднялся
// nowOnline = false -> ПК упал
typedef void (*StatusChangeCb)(PC& pc, bool nowOnline);

void monitorInit(StatusChangeCb cb);
void monitorTick();
