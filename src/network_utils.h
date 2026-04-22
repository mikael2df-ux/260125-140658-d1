#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Получить глобальный IP (api.ipify.org). Блокирующий, 1-2 сек.
String getPublicIP();

// Проверить, жив ли хост по IP (ICMP ping). Блокирующий на ~PING_TIMEOUT_MS.
bool pingHost(const IPAddress& ip);

// Форматировать RSSI / uptime / память — вспомогательное
String fmtUptime(uint32_t ms);
String fmtBytes(uint32_t bytes);
