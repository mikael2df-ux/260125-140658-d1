#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Отправить Magic Packet. Возвращает true если MAC распарсен.
bool wolSend(const String& mac, const IPAddress& broadcast);
