#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <FastBot2.h>

#include "secrets.h"
#include "config.h"
#include "pc_manager.h"
#include "monitor.h"
#include "bot_ui.h"
#include "gpio_ctrl.h"
#include "security.h"

// =============================================================
// Bot + whitelist
// =============================================================
FastBot2 bot;

static const int64_t ADMIN_LIST[] = ADMIN_IDS;
static constexpr size_t ADMIN_N = sizeof(ADMIN_LIST) / sizeof(ADMIN_LIST[0]);

static bool isAdmin(int64_t id) {
    for (size_t i = 0; i < ADMIN_N; i++)
        if (ADMIN_LIST[i] == id) return true;
    return false;
}

static uint32_t _startedAt = 0;

// =============================================================
// Update dispatcher
// =============================================================
static void onUpdate(fb::Update& u) {
    // Игнорируем апдейты первые N мс после старта (вдруг накопились)
    if (millis() - _startedAt < BOT_STARTUP_DELAY_MS) return;

    // Определяем id отправителя
    int64_t fromId = u.isQuery()
        ? (int64_t)u.query().from().id()
        : (int64_t)u.message().from().id();

    if (!isAdmin(fromId)) {
        Serial.printf("[BOT] DENY from %lld\n", (long long)fromId);
        if (u.isQuery()) {
            bot.answerCallbackQuery(u.query().id(), "🚫 Access denied", true);
        }
        return;
    }

    // Rate limit
    if (!rateCheck(fromId)) {
        Serial.printf("[BOT] RATE-LIMIT %lld\n", (long long)fromId);
        if (u.isQuery()) {
            bot.answerCallbackQuery(u.query().id(), "⏳ Слишком часто, попробуй позже", true);
        } else if (u.isMessage()) {
            bot.sendMessage(fb::Message("⏳ Rate limit — слишком часто.",
                                        u.message().chat().id()));
        }
        return;
    }

    if (u.isQuery()) {
        uiHandleQuery(u);
    } else if (u.isMessage()) {
        uiHandleMessage(u);
    }
}

// =============================================================
// Monitor callback -> Telegram notification
// =============================================================
static void onStatusChange(PC& pc, bool nowOnline) {
    Serial.printf("[MON] %s -> %s\n", pc.name.c_str(), nowOnline ? "ONLINE" : "OFFLINE");
    uiNotifyStatus(pc.name, nowOnline);
}

// =============================================================
// Setup / Loop
// =============================================================
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== WOL bot boot ==="));

    // LittleFS
    if (!LittleFS.begin()) {
        Serial.println(F("[FS] mount fail -> format"));
        LittleFS.format();
        LittleFS.begin();
    }
    pcm.load();
    gpioInitAll();

    // Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("[WiFi] connecting"));
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
        delay(200);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[WiFi] fail, reboot in 5s"));
        delay(5000);
        ESP.restart();
    }
    Serial.print(F("[WiFi] IP: "));
    Serial.println(WiFi.localIP());

    // NTP (для TLS)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print(F("[NTP] sync"));
    time_t now = time(nullptr);
    while (now < 24 * 3600) { delay(200); Serial.print('.'); now = time(nullptr); }
    Serial.println();

    // Bot
    bot.setToken(F(BOT_TOKEN));
    bot.skipUpdates();                              // пропустить накопленные
    // ВАЖНО: Async — callback в user ctx (как Sync), НО tick() не
    // блокируется на ожидании ответа, а возвращается быстро. Это даёт
    // loop() нормальную частоту и позволяет избегать WDT.
    // Перед отправкой сообщения из uiTick() проверяем bot.isPolling().
    bot.setPollMode(fb::Poll::Async, 4000);
    bot.attachUpdate(onUpdate);

    // UI & Monitor
    uiInit(&bot);
    monitorInit(onStatusChange);

    _startedAt = millis();

    // Приветствие админу
    bot.sendMessage(fb::Message(
        "🤖 WOL бот запущен\nIP: " + WiFi.localIP().toString(),
        NOTIFY_CHAT_ID
    ));
    logAction(0, "BOOT");
}

void loop() {
    bot.tick();
    yield();
    uiTick();
    yield();
    monitorTick();
    yield();
}
