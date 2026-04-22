#include "network_utils.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266Ping.h>
#include "config.h"

String getPublicIP() {
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(3000);
    if (!http.begin(client, "http://api.ipify.org")) return "unknown";
    int code = http.GET();
    String ip = (code == 200) ? http.getString() : String("err ") + code;
    http.end();
    return ip;
}

bool pingHost(const IPAddress& ip) {
    // 1 попытка, таймаут задаётся в секундах — минимум 1 сек в этой либе
    uint8_t timeoutSec = (PING_TIMEOUT_MS + 999) / 1000;
    if (timeoutSec < 1) timeoutSec = 1;
    return Ping.ping(ip, 1);  // count=1, таймаут по умолчанию библиотеки (~1 сек)
}

String fmtUptime(uint32_t ms) {
    uint32_t s = ms / 1000;
    uint32_t d = s / 86400;  s %= 86400;
    uint32_t h = s / 3600;   s %= 3600;
    uint32_t m = s / 60;     s %= 60;
    char buf[32];
    if (d) snprintf(buf, sizeof(buf), "%lud %luh %lum", (unsigned long)d, (unsigned long)h, (unsigned long)m);
    else if (h) snprintf(buf, sizeof(buf), "%luh %lum %lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else snprintf(buf, sizeof(buf), "%lum %lus", (unsigned long)m, (unsigned long)s);
    return String(buf);
}

String fmtBytes(uint32_t bytes) {
    char buf[24];
    if (bytes >= 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024)   snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else                      snprintf(buf, sizeof(buf), "%lu B", (unsigned long)bytes);
    return String(buf);
}
