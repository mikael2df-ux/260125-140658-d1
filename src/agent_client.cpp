#include "agent_client.h"
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "config.h"

static AgentResult _request(const PC& pc, const char* path, bool post) {
    AgentResult r{false, -1, ""};
    if (!pc.hasAgent()) return r;

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(AGENT_TIMEOUT_MS);

    String url = "http://" + pc.ip.toString() + ":" + String(pc.agentPort) + path;
    if (!http.begin(client, url)) {
        Serial.printf("[AGT] begin fail: %s\n", url.c_str());
        return r;
    }
    http.addHeader("Authorization", "Bearer " + pc.agentToken);
    http.addHeader("Content-Type", "application/json");

    int code = post ? http.POST("") : http.GET();
    r.httpCode = code;
    if (code > 0) {
        r.body = http.getString();
        r.ok = (code >= 200 && code < 300);
    }
    http.end();
    Serial.printf("[AGT] %s %s -> %d\n", post ? "POST" : "GET", url.c_str(), code);
    return r;
}

AgentResult agentStatus(const PC& pc) {
    return _request(pc, "/status", false);
}

AgentResult agentAction(const PC& pc, const char* action) {
    String path = "/";
    path += action;
    return _request(pc, path.c_str(), true);
}
