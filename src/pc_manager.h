#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

struct PC {
    String    name;
    String    mac;
    IPAddress broadcast;
    IPAddress ip;            // адрес для пинга (0.0.0.0 если не задан)
    bool      hasIP = false;

    String    group;         // имя группы, "" если нет
    int8_t    powerPin = -1; // GPIO на Power-кнопку (-1 = не задан)
    int8_t    resetPin = -1; // GPIO на Reset-кнопку

    // HTTP агент на ПК (опционально)
    uint16_t  agentPort  = 0;   // 0 = агент не настроен
    String    agentToken;       // Bearer

    bool hasAgent() const { return agentPort > 0 && agentToken.length() > 0 && hasIP; }

    // ---- runtime (не сохраняется на диск) ----
    bool      online        = false;
    uint8_t   missCount     = 0;     // промахи ping подряд
    uint32_t  lastChangeMs  = 0;
    uint32_t  wakeCount     = 0;
    uint32_t  onlineSeconds = 0;     // накопленное время online (since boot)
    uint32_t  totalSeconds  = 0;     // всего секунд мониторинга (since boot)

    // История 0/100 для графика (кольцевой буфер)
    uint8_t   history[HISTORY_POINTS] = {0};
    uint8_t   histIdx       = 0;
    bool      histFilled    = false;
    uint32_t  lastHistMs    = 0;

    // Uptime % за период истории
    float uptimePct() const {
        if (totalSeconds == 0) return 0.0f;
        return 100.0f * onlineSeconds / totalSeconds;
    }
};

class PCManager {
public:
    bool  load();
    bool  save();

    int   count() const        { return _count; }
    PC&   at(int i)             { return _pcs[i]; }
    const PC& at(int i) const  { return _pcs[i]; }

    int   findByName(const String& name) const;

    // idx в случае успеха, -1 если лимит/дубликат
    // autoSave=false позволяет объединить несколько правок в один save()
    int   add(const String& name, const String& mac,
              const IPAddress& bcast,
              const IPAddress& ip, bool hasIP,
              bool autoSave = true);

    bool  remove(int idx);

    // Группы
    bool  setGroup(int idx, const String& group);
    int   countGroups() const;
    String groupAt(int i) const;       // i-я уникальная группа
    int   pcsInGroup(const String& g, int* outIdx, int maxN) const;

    // GPIO
    bool  setGpio(int idx, int8_t powerPin, int8_t resetPin);

    // Agent
    bool  setAgent(int idx, uint16_t port, const String& token, bool autoSave = true);

private:
    PC  _pcs[MAX_PCS];
    int _count = 0;
};

extern PCManager pcm;
