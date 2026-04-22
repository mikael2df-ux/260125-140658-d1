#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "pc_manager.h"

struct AgentResult {
    bool    ok;
    int     httpCode;       // -1 если не подключились
    String  body;           // ответ сервера (может быть пустым)
};

// GET /status — возвращает JSON с инфо о ПК
AgentResult agentStatus(const PC& pc);

// POST action c пустым телом. Примеры action: "shutdown", "reboot", "sleep", "lock"
AgentResult agentAction(const PC& pc, const char* action);
