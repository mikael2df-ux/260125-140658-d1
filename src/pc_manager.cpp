#include "pc_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

PCManager pcm;

bool PCManager::load() {
    _count = 0;
    if (!LittleFS.exists(PCS_FILE)) {
        Serial.println(F("[PCM] no file, skip load"));
        return true;
    }
    File f = LittleFS.open(PCS_FILE, "r");
    if (!f) { Serial.println(F("[PCM] open fail")); return false; }

    // На heap — чтобы не съедать 2KB стека (критично в callback'ах FastBot2).
    DynamicJsonDocument doc(2048);
    auto err = deserializeJson(doc, f);
    f.close();
    if (err) { Serial.printf("[PCM] JSON err: %s\n", err.c_str()); return false; }

    JsonArray arr = doc["pcs"].as<JsonArray>();
    for (JsonObject o : arr) {
        if (_count >= MAX_PCS) break;
        PC& pc = _pcs[_count];
        pc = PC();
        pc.name = o["name"].as<String>();
        pc.mac  = o["mac"].as<String>();
        pc.broadcast.fromString(o["broadcast"].as<String>());
        const char* ipStr = o["ip"] | "";
        pc.hasIP = (ipStr && *ipStr) && pc.ip.fromString(ipStr);
        if (!pc.hasIP) pc.ip = IPAddress(0, 0, 0, 0);
        pc.group    = o["group"]    | "";
        pc.powerPin = o["powerPin"] | -1;
        pc.resetPin = o["resetPin"] | -1;
        pc.agentPort  = o["agentPort"]  | (uint16_t)0;
        pc.agentToken = o["agentToken"] | "";
        _count++;
    }
    Serial.printf("[PCM] loaded %d PCs\n", _count);
    return true;
}

bool PCManager::save() {
    DynamicJsonDocument doc(2048);   // heap, не стек
    JsonArray arr = doc.createNestedArray("pcs");
    for (int i = 0; i < _count; i++) {
        JsonObject o = arr.createNestedObject();
        o["name"]      = _pcs[i].name;
        o["mac"]       = _pcs[i].mac;
        o["broadcast"] = _pcs[i].broadcast.toString();
        if (_pcs[i].hasIP) o["ip"] = _pcs[i].ip.toString();
        if (_pcs[i].group.length()) o["group"] = _pcs[i].group;
        if (_pcs[i].powerPin >= 0)  o["powerPin"] = _pcs[i].powerPin;
        if (_pcs[i].resetPin >= 0)  o["resetPin"] = _pcs[i].resetPin;
        if (_pcs[i].agentPort > 0)  {
            o["agentPort"] = _pcs[i].agentPort;
            o["agentToken"] = _pcs[i].agentToken;
        }
    }
    File f = LittleFS.open(PCS_FILE, "w");
    if (!f) { Serial.println(F("[PCM] save open fail")); return false; }
    serializeJson(doc, f);
    f.close();
    Serial.printf("[PCM] saved %d PCs\n", _count);
    return true;
}

int PCManager::findByName(const String& name) const {
    for (int i = 0; i < _count; i++)
        if (_pcs[i].name.equalsIgnoreCase(name)) return i;
    return -1;
}

int PCManager::add(const String& name, const String& mac,
                   const IPAddress& bcast,
                   const IPAddress& ip, bool hasIP,
                   bool autoSave) {
    if (_count >= MAX_PCS) return -1;
    if (findByName(name) >= 0) return -1;
    PC& pc = _pcs[_count];
    pc = PC();
    pc.name = name;
    pc.mac  = mac;
    pc.broadcast = bcast;
    pc.ip = ip;
    pc.hasIP = hasIP;
    _count++;
    if (autoSave) save();
    return _count - 1;
}

bool PCManager::remove(int idx) {
    if (idx < 0 || idx >= _count) return false;
    for (int i = idx; i < _count - 1; i++) _pcs[i] = _pcs[i + 1];
    _count--;
    save();
    return true;
}

bool PCManager::setGroup(int idx, const String& group) {
    if (idx < 0 || idx >= _count) return false;
    _pcs[idx].group = group;
    save();
    return true;
}

int PCManager::countGroups() const {
    // считаем уникальные непустые группы
    String seen[MAX_PCS];
    int n = 0;
    for (int i = 0; i < _count; i++) {
        const String& g = _pcs[i].group;
        if (!g.length()) continue;
        bool dup = false;
        for (int j = 0; j < n; j++) if (seen[j] == g) { dup = true; break; }
        if (!dup) seen[n++] = g;
    }
    return n;
}

String PCManager::groupAt(int idx) const {
    String seen[MAX_PCS];
    int n = 0;
    for (int i = 0; i < _count; i++) {
        const String& g = _pcs[i].group;
        if (!g.length()) continue;
        bool dup = false;
        for (int j = 0; j < n; j++) if (seen[j] == g) { dup = true; break; }
        if (!dup) {
            if (n == idx) return g;
            seen[n++] = g;
        }
    }
    return String();
}

int PCManager::pcsInGroup(const String& g, int* outIdx, int maxN) const {
    int n = 0;
    for (int i = 0; i < _count && n < maxN; i++) {
        if (_pcs[i].group.equalsIgnoreCase(g)) outIdx[n++] = i;
    }
    return n;
}

bool PCManager::setGpio(int idx, int8_t powerPin, int8_t resetPin) {
    if (idx < 0 || idx >= _count) return false;
    _pcs[idx].powerPin = powerPin;
    _pcs[idx].resetPin = resetPin;
    save();
    return true;
}

bool PCManager::setAgent(int idx, uint16_t port, const String& token, bool autoSave) {
    if (idx < 0 || idx >= _count) return false;
    _pcs[idx].agentPort = port;
    _pcs[idx].agentToken = token;
    if (autoSave) save();
    return true;
}
