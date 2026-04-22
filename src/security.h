#pragma once
#include <Arduino.h>
#include "config.h"

// ---------- RATE LIMIT ----------
// Проверить и инкрементировать. Возвращает true если разрешено.
bool rateCheck(int64_t userId);

// ---------- ACTION LOG ----------
struct ActionLogEntry {
    uint32_t tsMs;            // millis() на момент события
    int64_t  userId;
    String   action;          // человекочитаемое описание
};

void  logAction(int64_t userId, const String& action);
int   logSize();
const ActionLogEntry& logAt(int i);   // новые сверху (i=0 — последнее)
