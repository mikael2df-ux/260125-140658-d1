#include "security.h"

// =================== Rate limit ===================
// Простой скользящий счётчик: таблица [userId -> (minute, count)]
// Для однопользовательского сценария этого более чем достаточно.
struct RateSlot {
    int64_t  userId = 0;
    uint32_t minute = 0;   // millis() / 60000
    uint16_t count  = 0;
};
static RateSlot _rate[4];     // до 4 юзеров — нам хватит

bool rateCheck(int64_t userId) {
    uint32_t curMin = millis() / 60000UL;
    // найти или выделить слот
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (_rate[i].userId == userId) { slot = i; break; }
    }
    if (slot < 0) {
        for (int i = 0; i < 4; i++) {
            if (_rate[i].userId == 0) { slot = i; _rate[i].userId = userId; break; }
        }
    }
    if (slot < 0) slot = 0;    // вытеснить, если нет места

    if (_rate[slot].minute != curMin) {
        _rate[slot].minute = curMin;
        _rate[slot].count = 0;
    }
    _rate[slot].count++;
    return _rate[slot].count <= RATE_LIMIT_PER_MIN;
}

// =================== Action log ===================
static ActionLogEntry _log[ACTION_LOG_SIZE];
static int _logHead = 0;   // индекс, куда писать следующую запись
static int _logN    = 0;   // сколько заполнено (<=ACTION_LOG_SIZE)

void logAction(int64_t userId, const String& action) {
    _log[_logHead].tsMs = millis();
    _log[_logHead].userId = userId;
    _log[_logHead].action = action;
    _logHead = (_logHead + 1) % ACTION_LOG_SIZE;
    if (_logN < ACTION_LOG_SIZE) _logN++;

    Serial.printf("[LOG] %lld: %s\n", (long long)userId, action.c_str());
}

int logSize() { return _logN; }

const ActionLogEntry& logAt(int i) {
    // i=0 — самое свежее
    int idx = (_logHead - 1 - i + ACTION_LOG_SIZE * 2) % ACTION_LOG_SIZE;
    return _log[idx];
}
