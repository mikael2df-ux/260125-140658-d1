#include "monitor.h"
#include "network_utils.h"
#include "config.h"

static StatusChangeCb _cb = nullptr;
static uint32_t       _lastTick = 0;
static uint32_t       _lastStatsTick = 0;
static int            _nextIdx = 0;

void monitorInit(StatusChangeCb cb) {
    _cb = cb;
    _lastTick = millis();
    _lastStatsTick = millis();
}

// Раз в секунду копим счётчики online/total для всех ПК с IP
static void _accumulateStats() {
    uint32_t now = millis();
    uint32_t dt = now - _lastStatsTick;
    if (dt < 1000) return;
    uint32_t sec = dt / 1000;
    _lastStatsTick = now - (dt - sec * 1000);
    for (int i = 0; i < pcm.count(); i++) {
        PC& pc = pcm.at(i);
        if (!pc.hasIP) continue;
        pc.totalSeconds += sec;
        if (pc.online) pc.onlineSeconds += sec;
    }
}

// Раз в HISTORY_INTERVAL_MS — запись точки в кольцевой буфер
static void _pushHistory() {
    uint32_t now = millis();
    for (int i = 0; i < pcm.count(); i++) {
        PC& pc = pcm.at(i);
        if (!pc.hasIP) continue;
        if (pc.lastHistMs == 0) { pc.lastHistMs = now; continue; }
        if (now - pc.lastHistMs < HISTORY_INTERVAL_MS) continue;
        pc.lastHistMs = now;
        pc.history[pc.histIdx] = pc.online ? 100 : 0;
        pc.histIdx = (pc.histIdx + 1) % HISTORY_POINTS;
        if (pc.histIdx == 0) pc.histFilled = true;
    }
}

void monitorTick() {
    _accumulateStats();
    _pushHistory();

    if (pcm.count() == 0) return;
    if (millis() - _lastTick < MONITOR_INTERVAL_MS) return;
    _lastTick = millis();

    if (_nextIdx >= pcm.count()) _nextIdx = 0;
    PC& pc = pcm.at(_nextIdx++);
    if (!pc.hasIP) return;

    bool alive = pingHost(pc.ip);
    // pingHost блокирует до ~1 сек — покормим watchdog перед callback
    yield();
    ESP.wdtFeed();

    if (alive) {
        pc.missCount = 0;
        if (!pc.online) {
            pc.online = true;
            pc.lastChangeMs = millis();
            if (_cb) _cb(pc, true);
        }
    } else {
        if (pc.missCount < 255) pc.missCount++;
        if (pc.online && pc.missCount >= MONITOR_OFFLINE_HITS) {
            pc.online = false;
            pc.lastChangeMs = millis();
            if (_cb) _cb(pc, false);
        }
    }
}
