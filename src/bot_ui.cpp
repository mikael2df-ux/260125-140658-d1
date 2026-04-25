#include "bot_ui.h"
#include "pc_manager.h"
#include "wol.h"
#include "network_utils.h"
#include "gpio_ctrl.h"
#include "security.h"
#include "agent_client.h"
#include "config.h"
#include "secrets.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <CharPlot.h>

static FastBot2* _bot = nullptr;

// =========================================================
// Отложенная QR-проверка: callback FastBot2 не должен делать
// новых сетевых запросов (иначе реентрант TCP стека → crash).
// После добавления ПК ставим pending, а ping+agent проверку
// делаем уже из uiTick() в loop().
// =========================================================
enum PendStage : uint8_t {
    PS_IDLE = 0,
    PS_PARSE,        // распарсить rawPayload и добавить/обновить ПК
    PS_SEND_ADDED,   // отправить "✅ добавлен, проверяю..."
    PS_DO_CHECK,     // сделать ping+agent+итог
};

struct PendingCheck {
    PendStage stage = PS_IDLE;
    int       idx;
    fb::ID    chatID;
    int64_t   userId;
    String    rawPayload;   // используется в PS_PARSE
    String    name;         // заполняется после парсинга
    bool      isNew;
    uint32_t  nextAt;
};
static PendingCheck _pending;

// Очередь уведомлений 🟢/🔴 от monitor callback:
// в callback НЕ шлём sendMessage (блокирует → WDT),
// а ставим в очередь и отправляем по одной из uiTick().
#define NOTIFY_QUEUE_SIZE 4
struct NotifyItem {
    String name;
    bool   online;
    bool   used = false;
};
static NotifyItem _notifyQueue[NOTIFY_QUEUE_SIZE];
static uint32_t   _notifyNextAt = 0;

// =========================================================
// Helpers
// =========================================================

static String statusEmoji(const PC& pc) {
    if (!pc.hasIP) return "⚪";
    return pc.online ? "🟢" : "🔴";
}

static String fmtPct(float v) {
    char b[12]; snprintf(b, sizeof(b), "%.1f%%", v); return String(b);
}

// Построить главное меню (текст + клавиатура)
static String buildMainText() {
    String t = "🖥 <b>WOL Bot</b>\n";
    t += "Uptime: " + fmtUptime(millis()) + "\n";
    t += "RSSI: " + String(WiFi.RSSI()) + " dBm\n\n";
    if (pcm.count() == 0) {
        t += "<i>Список ПК пуст.</i>\nДобавь через ➕";
    } else {
        t += "Всего ПК: " + String(pcm.count()) + " / " + String(MAX_PCS) + "\n";
        for (int i = 0; i < pcm.count(); i++) {
            const PC& pc = pcm.at(i);
            t += statusEmoji(pc) + " " + pc.name;
            if (pc.group.length()) t += " <i>(" + pc.group + ")</i>";
            t += "\n";
        }
    }
    return t;
}

static void buildMainKb(fb::InlineKeyboard& kb) {
    kb.addButton("⚡ Включить", "m_wol").addButton("📋 ПК", "m_list").newRow();
    kb.addButton("👥 Группы", "m_grp").addButton("📊 Стата", "m_stat").newRow();
    kb.addButton("➕ Доб.", "m_add").addButton("❌ Удал.", "m_del").newRow();
    kb.addButton("📜 Лог", "m_log").addButton("🌐 IP", "m_ip").newRow();
    kb.addButton("🔄 Обновить", "m_main");
}

static void buildPcListKb(fb::InlineKeyboard& kb, const char* prefix, bool withBack = true) {
    for (int i = 0; i < pcm.count(); i++) {
        const PC& pc = pcm.at(i);
        String label = statusEmoji(pc) + " " + pc.name;
        String data  = String(prefix) + String(i);
        kb.addButton(label, data).newRow();
    }
    if (withBack) kb.addButton("⬅ Назад", "m_main");
}

// Редактировать текущее сообщение (с новой клавиатурой)
static void editCurrent(fb::Update& u, const String& text, fb::InlineKeyboard* kb) {
    fb::TextEdit e;
    e.text = text;
    e.chatID = u.query().message().chat().id();
    e.messageID = (uint32_t)u.query().message().id().toInt32();
    e.mode = fb::Message::Mode::HTML;
    if (kb) e.setKeyboard(kb);
    _bot->editText(e);
}

static void sendHTML(fb::ID chatID, const String& text, fb::InlineKeyboard* kb = nullptr) {
    fb::Message m;
    m.text = text;
    m.chatID = chatID;
    m.mode = fb::Message::Mode::HTML;
    if (kb) m.setKeyboard(kb);
    _bot->sendMessage(m);
}

// =========================================================
// Init / Main menu
// =========================================================

void uiInit(FastBot2* bot) {
    _bot = bot;
}

void uiShowMainMenu(fb::ID chatID) {
    fb::InlineKeyboard kb;
    buildMainKb(kb);
    sendHTML(chatID, buildMainText(), &kb);
}

// =========================================================
// Экраны
// =========================================================

static void screenMain(fb::Update& u) {
    fb::InlineKeyboard kb; buildMainKb(kb);
    editCurrent(u, buildMainText(), &kb);
}

static void screenList(fb::Update& u) {
    String t = "📋 <b>Список ПК</b>\n\n";
    if (pcm.count() == 0) {
        t += "<i>Пусто</i>";
    } else {
        for (int i = 0; i < pcm.count(); i++) {
            const PC& pc = pcm.at(i);
            t += statusEmoji(pc) + " <b>" + pc.name + "</b>";
            if (pc.group.length()) t += " <i>[" + pc.group + "]</i>";
            t += "\n";
            t += "  MAC: <code>" + pc.mac + "</code>\n";
            t += "  BCAST: <code>" + pc.broadcast.toString() + "</code>\n";
            if (pc.hasIP) t += "  IP: <code>" + pc.ip.toString() + "</code>\n";
            if (pc.powerPin >= 0) t += "  🔌 Power pin: D" + String(pc.powerPin) + "\n";
            if (pc.resetPin >= 0) t += "  🔁 Reset pin: D" + String(pc.resetPin) + "\n";
            if (pc.wakeCount) t += "  ⚡ будили: " + String(pc.wakeCount) + "\n";
            t += "\n";
        }
    }
    fb::InlineKeyboard kb;
    for (int i = 0; i < pcm.count(); i++) {
        kb.addButton("🔎 " + pcm.at(i).name, "info_" + String(i)).newRow();
    }
    kb.addButton("⬅ Назад", "m_main");
    editCurrent(u, t, &kb);
}

static void screenPcInfo(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) { screenMain(u); return; }
    const PC& pc = pcm.at(idx);
    String t = "🔎 <b>" + pc.name + "</b> " + statusEmoji(pc) + "\n\n";
    t += "MAC: <code>" + pc.mac + "</code>\n";
    t += "BCAST: <code>" + pc.broadcast.toString() + "</code>\n";
    if (pc.hasIP) {
        t += "IP: <code>" + pc.ip.toString() + "</code>\n";
        t += "Uptime: " + fmtPct(pc.uptimePct()) + " (" + fmtUptime(pc.onlineSeconds * 1000UL) + ")\n";
    }
    if (pc.group.length()) t += "Группа: <i>" + pc.group + "</i>\n";
    if (pc.hasAgent()) t += "🤖 Агент: <code>" + pc.ip.toString() + ":" + String(pc.agentPort) + "</code>\n";
    t += "⚡ будили: " + String(pc.wakeCount) + " раз\n";

    fb::InlineKeyboard kb;
    kb.addButton("⚡ WOL", "wake_" + String(idx));
    if (pc.hasIP) kb.addButton("📈 График", "chart_" + String(idx));
    kb.newRow();
    // Агент — soft-управление ОС
    if (pc.hasAgent()) {
        kb.addButton("📊 Status", "ast_" + String(idx));
        kb.addButton("🔒 Lock", "alk_" + String(idx)).newRow();
        kb.addButton("💤 Sleep", "asl_" + String(idx));
        kb.addButton("🔃 Reboot", "arb_" + String(idx));
        kb.addButton("⏻ Shutdown", "ash_" + String(idx)).newRow();
    }
    if (pc.powerPin >= 0) {
        kb.addButton("🔌 Power", "pwr_" + String(idx));
        kb.addButton("⏻ Force off", "pwrL_" + String(idx)).newRow();
    }
    if (pc.resetPin >= 0) {
        kb.addButton("🔁 Hard reset", "rst_" + String(idx)).newRow();
    }
    kb.addButton("⬅ Назад", "m_list");
    editCurrent(u, t, &kb);
}

static void screenChart(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) { screenMain(u); return; }
    PC& pc = pcm.at(idx);
    if (!pc.hasIP) {
        fb::InlineKeyboard kb; kb.addButton("⬅ Назад", "info_" + String(idx));
        editCurrent(u, "⚠ Мониторинг не настроен (нет IP)", &kb);
        return;
    }

    // Собираем данные: самые старые слева
    int n = pc.histFilled ? HISTORY_POINTS : pc.histIdx;
    if (n < 2) {
        fb::InlineKeyboard kb; kb.addButton("⬅ Назад", "info_" + String(idx));
        editCurrent(u, "⏳ Данных мало (" + String(n) + " точек)\nПервый слот каждые "
                     + String(HISTORY_INTERVAL_MS / 60000) + " мин", &kb);
        return;
    }

    float data[HISTORY_POINTS];
    int start = pc.histFilled ? pc.histIdx : 0;
    for (int i = 0; i < n; i++) {
        int realIdx = (start + i) % HISTORY_POINTS;
        data[i] = pc.history[realIdx];
    }

    String plot = CharPlot<COLON_X2>(data, n, CHART_HEIGHT, 1, 0);

    String t = "📈 <b>" + pc.name + "</b> — uptime history\n";
    t += "Период: " + String((uint32_t)HISTORY_INTERVAL_MS / 60000) + " мин/точка, "
         + String(n) + " точек\n";
    t += "Общий uptime: " + fmtPct(pc.uptimePct()) + "\n";
    t += "<pre>" + plot + "</pre>";

    fb::InlineKeyboard kb;
    kb.addButton("🔄 Обновить", "chart_" + String(idx)).newRow();
    kb.addButton("⬅ Назад", "info_" + String(idx));
    editCurrent(u, t, &kb);
}

static void screenWol(fb::Update& u) {
    if (pcm.count() == 0) {
        fb::InlineKeyboard kb; kb.addButton("⬅ Назад", "m_main");
        editCurrent(u, "❌ Нет добавленных ПК", &kb);
        return;
    }
    fb::InlineKeyboard kb;
    buildPcListKb(kb, "wake_", true);
    editCurrent(u, "⚡ <b>Выбери ПК для пробуждения:</b>", &kb);
}

static void screenDel(fb::Update& u) {
    if (pcm.count() == 0) {
        fb::InlineKeyboard kb; kb.addButton("⬅ Назад", "m_main");
        editCurrent(u, "❌ Нет добавленных ПК", &kb);
        return;
    }
    fb::InlineKeyboard kb;
    buildPcListKb(kb, "delq_", true);   // delq_ = delete-ask confirm
    editCurrent(u, "❌ <b>Выбери ПК для удаления:</b>", &kb);
}

static void screenAdd(fb::Update& u) {
    String t = "➕ <b>Добавить ПК</b>\n\n";
    t += "<code>/add NAME MAC BCAST [IP]</code>\n\n";
    t += "Примеры:\n";
    t += "<code>/add PC1 AA:BB:CC:DD:EE:FF 192.168.1.255</code>\n";
    t += "<code>/add NAS 11:22:33:44:55:66 192.168.1.255 192.168.1.10</code>\n\n";
    t += "Дополнительно:\n";
    t += "<code>/group NAME GROUP</code> — назначить группу (или '-' чтобы убрать)\n";
    t += "<code>/gpio NAME POWER_PIN RESET_PIN</code> — GPIO для физ. управления (-1 чтобы выкл)\n";
    fb::InlineKeyboard kb;
    kb.addButton("⬅ Назад", "m_main");
    editCurrent(u, t, &kb);
}

static void screenIP(fb::Update& u) {
    fb::InlineKeyboard kb;
    kb.addButton("⬅ Назад", "m_main");
    editCurrent(u, "⏳ Получаю IP...", &kb);
    String ip = getPublicIP();
    editCurrent(u, "🌐 <b>Глобальный IP:</b>\n<code>" + ip + "</code>", &kb);
}

static void screenStat(fb::Update& u) {
    String t = "📊 <b>Статистика</b>\n\n";
    t += "Uptime бота: " + fmtUptime(millis()) + "\n";
    t += "Free heap: " + fmtBytes(ESP.getFreeHeap()) + "\n";
    t += "WiFi RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    t += "Local IP: <code>" + WiFi.localIP().toString() + "</code>\n\n";
    t += "<b>ПК:</b>\n";
    if (pcm.count() == 0) {
        t += "<i>нет данных</i>";
    } else {
        for (int i = 0; i < pcm.count(); i++) {
            const PC& pc = pcm.at(i);
            t += statusEmoji(pc) + " <b>" + pc.name + "</b>";
            if (pc.hasIP) {
                t += " — " + fmtPct(pc.uptimePct());
            }
            t += "\n  ⚡ " + String(pc.wakeCount) + " раз\n";
        }
    }
    fb::InlineKeyboard kb;
    kb.addButton("⬅ Назад", "m_main");
    editCurrent(u, t, &kb);
}

static void screenGroups(fb::Update& u) {
    String t = "👥 <b>Группы</b>\n\n";
    int nG = pcm.countGroups();
    if (nG == 0) {
        t += "<i>Группы не заданы.</i>\n\n"
             "Назначить группу: <code>/group PC_NAME GROUP</code>\n"
             "Убрать: <code>/group PC_NAME -</code>";
        fb::InlineKeyboard kb; kb.addButton("⬅ Назад", "m_main");
        editCurrent(u, t, &kb);
        return;
    }
    fb::InlineKeyboard kb;
    for (int i = 0; i < nG; i++) {
        String g = pcm.groupAt(i);
        int idxs[MAX_PCS]; int n = pcm.pcsInGroup(g, idxs, MAX_PCS);
        int online = 0;
        for (int j = 0; j < n; j++) if (pcm.at(idxs[j]).online) online++;
        t += "• <b>" + g + "</b> — " + String(n) + " ПК ("
             + String(online) + " 🟢)\n";
        kb.addButton("⚡ " + g + " (будить " + String(n) + ")", "gwake_" + String(i)).newRow();
    }
    kb.addButton("⬅ Назад", "m_main");
    editCurrent(u, t, &kb);
}

static void screenLog(fb::Update& u) {
    String t = "📜 <b>Последние действия</b>\n\n";
    int n = logSize();
    if (n == 0) {
        t += "<i>лог пуст</i>";
    } else {
        uint32_t now = millis();
        for (int i = 0; i < n && i < 10; i++) {
            const ActionLogEntry& e = logAt(i);
            uint32_t ago = (now - e.tsMs) / 1000;
            t += "• " + fmtUptime(ago * 1000) + " назад — ";
            t += e.action + "\n";
        }
    }
    fb::InlineKeyboard kb;
    kb.addButton("🔄", "m_log").addButton("⬅ Назад", "m_main");
    editCurrent(u, t, &kb);
}

// =========================================================
// Действия (wake / del / hard power / reset / chart)
// =========================================================

static int64_t queryUserId(fb::Update& u) {
    return (int64_t)u.query().from().id();
}

static void doWake(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    bool ok = wolSend(pc.mac, pc.broadcast);
    if (ok) pc.wakeCount++;
    logAction(queryUserId(u), (ok ? "WOL " : "WOL-ERR ") + pc.name);
    _bot->answerCallbackQuery(u.query().id(),
        ok ? ("⚡ " + pc.name + " — WOL отправлен") : "❌ Ошибка MAC", true);
    screenPcInfo(u, idx);
}

// Подтверждение удаления
static void screenDelConfirm(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) { screenMain(u); return; }
    String t = "❓ Точно удалить <b>" + pcm.at(idx).name + "</b>?";
    fb::InlineKeyboard kb;
    kb.addButton("✅ Да, удалить", "del_" + String(idx)).newRow();
    kb.addButton("⬅ Отмена", "m_del");
    editCurrent(u, t, &kb);
}

static void doDel(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    String nm = pcm.at(idx).name;
    pcm.remove(idx);
    logAction(queryUserId(u), "DEL " + nm);
    _bot->answerCallbackQuery(u.query().id(), "🗑 " + nm + " удалён", false);
    screenMain(u);
}

// Подтверждение для разрушительных GPIO-действий
static void screenPwrConfirm(fb::Update& u, int idx, const char* kind, const char* action) {
    String t = "⚠ <b>" + String(kind) + "</b> для <b>" + pcm.at(idx).name + "</b>?\n\n";
    t += "<i>Это физическое замыкание контактов " + String(kind) + ".</i>";
    fb::InlineKeyboard kb;
    kb.addButton("✅ Выполнить", String(action) + "_" + String(idx)).newRow();
    kb.addButton("⬅ Отмена", "info_" + String(idx));
    editCurrent(u, t, &kb);
}

static void doPwrShort(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    if (pc.powerPin < 0) return;
    gpioPulse(pc.powerPin, GPIO_POWER_PULSE_MS);
    logAction(queryUserId(u), "PWR-SHORT " + pc.name);
    _bot->answerCallbackQuery(u.query().id(), "🔌 Power импульс отправлен", true);
    screenPcInfo(u, idx);
}

static void doPwrLong(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    if (pc.powerPin < 0) return;
    _bot->answerCallbackQuery(u.query().id(),
        "⏻ Удерживаю Power " + String(GPIO_POWER_LONG_MS/1000) + " с...", true);
    gpioPulse(pc.powerPin, GPIO_POWER_LONG_MS);
    logAction(queryUserId(u), "PWR-LONG " + pc.name);
    screenPcInfo(u, idx);
}

static void doReset(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    if (pc.resetPin < 0) return;
    gpioPulse(pc.resetPin, GPIO_RESET_PULSE_MS);
    logAction(queryUserId(u), "RESET " + pc.name);
    _bot->answerCallbackQuery(u.query().id(), "🔁 Reset отправлен", true);
    screenPcInfo(u, idx);
}

// Мини-бар прогресса для <pre>-блока. value 0..100
static String progressBar(float value, int width = 10) {
    if (isnan(value) || value < 0) value = 0;
    if (value > 100) value = 100;
    int filled = (int)((value / 100.0f) * width + 0.5f);
    String s;
    s.reserve(width);
    for (int i = 0; i < width; i++) s += (i < filled) ? "▓" : "░";
    return s;
}

// ---- Agent actions ----
static void doAgentStatus(fb::Update& u, int idx) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    _bot->answerCallbackQuery(u.query().id(), "⏳ Запрашиваю...", false);
    AgentResult r = agentStatus(pc);
    logAction(queryUserId(u), "AGT-STATUS " + pc.name);

    String t = "🤖 <b>" + pc.name + "</b> — Agent status\n\n";

    if (!r.ok) {
        t += "❌ HTTP " + String(r.httpCode) + "\n";
        if (r.body.length()) t += "<i>" + r.body + "</i>\n";
        else                 t += "<i>no reply</i>\n";
        fb::InlineKeyboard kb;
        kb.addButton("🔄", "ast_" + String(idx)).addButton("⬅ Назад", "info_" + String(idx));
        editCurrent(u, t, &kb);
        return;
    }

    // Парсим JSON ответ агента
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, r.body);
    if (err) {
        t += "⚠ JSON err: " + String(err.c_str()) + "\n<pre>" + r.body + "</pre>";
    } else {
        const char* host  = doc["host"]  | "?";
        const char* os    = doc["os"]    | "?";
        const char* ipStr = doc["ip"]    | "?";
        const char* ver   = doc["agent"] | "?";
        uint32_t upSec    = doc["uptime"] | 0;
        float cpu         = doc["cpu"].isNull() ? NAN : doc["cpu"].as<float>();
        float ram         = doc["ram"].isNull() ? NAN : doc["ram"].as<float>();

        // HTML-строки с информацией
        t += "🖥 <b>Host:</b>   " + String(host)  + "\n";
        t += "💻 <b>OS:</b>     " + String(os)    + "\n";
        t += "🌐 <b>IP:</b>     <code>" + String(ipStr) + "</code>\n";
        t += "⏱ <b>Uptime:</b> " + fmtUptime(upSec * 1000UL) + "\n";

        // CPU / RAM с прогресс-барами (в <pre>-блоке для моноширного)
        t += "\n<pre>";
        if (!isnan(cpu)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "CPU  %s %5.1f%%\n",
                     progressBar(cpu).c_str(), cpu);
            t += buf;
        } else {
            t += "CPU  n/a (psutil?)\n";
        }
        if (!isnan(ram)) {
            char buf[64];
            snprintf(buf, sizeof(buf), "RAM  %s %5.1f%%",
                     progressBar(ram).c_str(), ram);
            t += buf;
        } else {
            t += "RAM  n/a (psutil?)";
        }
        t += "</pre>\n";

        t += "\n<i>agent v" + String(ver) + "</i>";
    }

    fb::InlineKeyboard kb;
    kb.addButton("🔄 Обновить", "ast_" + String(idx)).newRow();
    kb.addButton("⬅ Назад", "info_" + String(idx));
    editCurrent(u, t, &kb);
}

static void screenAgentConfirm(fb::Update& u, int idx, const char* title, const char* action) {
    if (idx < 0 || idx >= pcm.count()) return;
    String t = "⚠ <b>" + String(title) + "</b>?\n\n";
    t += "ПК: <b>" + pcm.at(idx).name + "</b>";
    fb::InlineKeyboard kb;
    kb.addButton("✅ Да", String(action) + "_" + String(idx)).newRow();
    kb.addButton("⬅ Отмена", "info_" + String(idx));
    editCurrent(u, t, &kb);
}

static void doAgentAction(fb::Update& u, int idx, const char* action, const char* successMsg) {
    if (idx < 0 || idx >= pcm.count()) return;
    PC& pc = pcm.at(idx);
    _bot->answerCallbackQuery(u.query().id(), "⏳ Отправляю " + String(action) + "...", false);
    AgentResult r = agentAction(pc, action);
    logAction(queryUserId(u), "AGT-" + String(action) + " " + pc.name);

    String t;
    if (r.ok) {
        t = String("✅ <b>") + pc.name + "</b>: " + successMsg;
    } else {
        t = "❌ <b>" + pc.name + "</b>: ошибка (HTTP " + String(r.httpCode) + ")\n";
        if (r.body.length()) t += "<i>" + r.body + "</i>";
    }
    fb::InlineKeyboard kb;
    kb.addButton("⬅ Назад", "info_" + String(idx));
    editCurrent(u, t, &kb);
}

static void doGroupWake(fb::Update& u, int groupIdx) {
    String g = pcm.groupAt(groupIdx);
    if (!g.length()) return;
    int idxs[MAX_PCS]; int n = pcm.pcsInGroup(g, idxs, MAX_PCS);
    int ok = 0;
    for (int i = 0; i < n; i++) {
        PC& pc = pcm.at(idxs[i]);
        if (wolSend(pc.mac, pc.broadcast)) { pc.wakeCount++; ok++; }
        delay(100);    // пауза между пакетами
    }
    logAction(queryUserId(u), "GROUP-WAKE " + g + " (" + String(ok) + "/" + String(n) + ")");
    _bot->answerCallbackQuery(u.query().id(),
        "⚡ Будим группу " + g + ": " + String(ok) + "/" + String(n), true);
    screenGroups(u);
}

// =========================================================
// Query dispatcher
// =========================================================

void uiHandleQuery(fb::Update& u) {
    Text data = u.query().data();
    Serial.printf("[Q] data='%s'\n", String(data).c_str());

    auto idxFromData = [&](int skip) -> int {
        return String(data).substring(skip).toInt();
    };

    if (data.startsWith("wake_"))  { doWake(u, idxFromData(5));  return; }
    if (data.startsWith("delq_"))  { screenDelConfirm(u, idxFromData(5)); return; }
    if (data.startsWith("del_"))   { doDel(u, idxFromData(4));   return; }
    if (data.startsWith("info_"))  { screenPcInfo(u, idxFromData(5)); return; }
    if (data.startsWith("chart_")) { screenChart(u, idxFromData(6)); return; }
    if (data.startsWith("pwrL_"))  { screenPwrConfirm(u, idxFromData(5), "Force OFF (long)", "dpwL"); return; }
    if (data.startsWith("pwr_"))   { screenPwrConfirm(u, idxFromData(4), "Power", "dpw"); return; }
    if (data.startsWith("rst_"))   { screenPwrConfirm(u, idxFromData(4), "Hard Reset", "drst"); return; }
    if (data.startsWith("dpwL_"))  { doPwrLong(u, idxFromData(5)); return; }
    if (data.startsWith("dpw_"))   { doPwrShort(u, idxFromData(4)); return; }
    if (data.startsWith("drst_"))  { doReset(u, idxFromData(5)); return; }
    if (data.startsWith("gwake_")) { doGroupWake(u, idxFromData(6)); return; }

    // Agent confirmations (ask)
    if (data.startsWith("alk_"))  { screenAgentConfirm(u, idxFromData(4), "Lock",     "dalk");  return; }
    if (data.startsWith("asl_"))  { screenAgentConfirm(u, idxFromData(4), "Sleep",    "dasl");  return; }
    if (data.startsWith("arb_"))  { screenAgentConfirm(u, idxFromData(4), "Reboot",   "darb");  return; }
    if (data.startsWith("ash_"))  { screenAgentConfirm(u, idxFromData(4), "Shutdown", "dash");  return; }
    if (data.startsWith("ast_"))  { doAgentStatus(u, idxFromData(4)); return; }
    // Agent actions (do)
    if (data.startsWith("dalk_")) { doAgentAction(u, idxFromData(5), "lock",     "заблокирован"); return; }
    if (data.startsWith("dasl_")) { doAgentAction(u, idxFromData(5), "sleep",    "уходит в сон"); return; }
    if (data.startsWith("darb_")) { doAgentAction(u, idxFromData(5), "reboot",   "перезагрузка"); return; }
    if (data.startsWith("dash_")) { doAgentAction(u, idxFromData(5), "shutdown", "выключается"); return; }

    switch (data.hash()) {
        case "m_main"_h:  screenMain(u); break;
        case "m_list"_h:  screenList(u); break;
        case "m_wol"_h:   screenWol(u);  break;
        case "m_del"_h:   screenDel(u);  break;
        case "m_add"_h:   screenAdd(u);  break;
        case "m_ip"_h:    screenIP(u);   break;
        case "m_stat"_h:  screenStat(u); break;
        case "m_grp"_h:   screenGroups(u); break;
        case "m_log"_h:   screenLog(u);  break;
        default:
            _bot->answerCallbackQuery(u.query().id(), "⚠ unknown", false);
            break;
    }
}

// =========================================================
// Text messages
// =========================================================

static void cmdAdd(fb::Update& u, const String& text) {
    fb::ID chatID = u.message().chat().id();
    if (pcm.count() >= MAX_PCS) {
        _bot->sendMessage(fb::Message("❌ Лимит ПК (" + String(MAX_PCS) + ")", chatID));
        return;
    }
    char name[32], mac[32], bcast[32], ipBuf[32] = "";
    int parsed = sscanf(text.c_str(), "/add %31s %31s %31s %31s",
                        name, mac, bcast, ipBuf);
    if (parsed < 3) {
        _bot->sendMessage(fb::Message("❌ Формат: /add NAME MAC BCAST [IP]", chatID));
        return;
    }
    IPAddress bc, ip(0, 0, 0, 0);
    if (!bc.fromString(bcast)) {
        _bot->sendMessage(fb::Message("❌ Неверный broadcast: " + String(bcast), chatID));
        return;
    }
    bool hasIP = (parsed == 4 && ip.fromString(ipBuf));

    int idx = pcm.add(String(name), String(mac), bc, ip, hasIP);
    if (idx < 0) {
        _bot->sendMessage(fb::Message("❌ Не удалось (дубликат/лимит)", chatID));
        return;
    }
    logAction((int64_t)u.message().from().id(), "ADD " + String(name));
    String ok = "✅ Добавлен: " + String(name);
    if (hasIP) ok += " (ping: " + ip.toString() + ")";
    _bot->sendMessage(fb::Message(ok, chatID));
    uiShowMainMenu(chatID);
}

static void cmdGroup(fb::Update& u, const String& text) {
    fb::ID chatID = u.message().chat().id();
    char name[32], grp[32];
    int parsed = sscanf(text.c_str(), "/group %31s %31s", name, grp);
    if (parsed < 2) {
        _bot->sendMessage(fb::Message("Формат: /group NAME GROUP (или '-' чтобы убрать)", chatID));
        return;
    }
    int idx = pcm.findByName(String(name));
    if (idx < 0) {
        _bot->sendMessage(fb::Message("❌ ПК не найден: " + String(name), chatID));
        return;
    }
    String g = String(grp);
    if (g == "-") g = "";
    pcm.setGroup(idx, g);
    logAction((int64_t)u.message().from().id(), "SETGROUP " + String(name) + " -> " + g);
    _bot->sendMessage(fb::Message(
        "✅ " + String(name) + ": группа → " + (g.length() ? g : String("(нет)")),
        chatID));
}

// Парсинг QR-строки: WOLBOT|v1|NAME|MAC|BCAST|IP|PORT|TOKEN
// ВАЖНО: внутри callback FastBot2 стек уже сильно занят (на ESP8266 ~4KB total).
// Здесь НИ парсинга, НИ LittleFS-работы — только копирование текста в pending.
// Вся тяжёлая работа в uiTick() → case PS_PARSE.
static bool tryHandleQRPayload(fb::Update& u, const String& text) {
    if (!text.startsWith(QR_PREFIX)) return false;
    Serial.println(F("[QR] payload captured, deferring"));

    _pending.stage = PS_PARSE;
    _pending.rawPayload = text;
    _pending.chatID = u.message().chat().id();
    _pending.userId = (int64_t)u.message().from().id();
    _pending.nextAt = millis() + 100;
    return true;
}

// Собственно парсинг и запись на диск — уже в user-loop контексте
// со свежим стеком.
static void doParseQR() {
    Serial.println(F("[QR] parsing payload"));
    String text = _pending.rawPayload;
    fb::ID chatID = _pending.chatID;
    int64_t userId = _pending.userId;

    _pending.rawPayload = "";   // освободить раньше

    String rest = text.substring(strlen(QR_PREFIX));
    String fields[6];
    int cnt = 0, start = 0;
    for (int i = 0; i <= (int)rest.length() && cnt < 6; i++) {
        if (i == (int)rest.length() || rest[i] == '|') {
            fields[cnt++] = rest.substring(start, i);
            start = i + 1;
        }
    }
    if (cnt < 6) {
        sendHTML(chatID, "❌ Неверный формат QR (ожидалось 6 полей, получено " + String(cnt) + ")");
        _pending.stage = PS_IDLE;
        return;
    }

    String name  = fields[0];
    String mac   = fields[1];
    String bcast = fields[2];
    String ipStr = fields[3];
    int    port  = fields[4].toInt();
    String token = fields[5];
    token.trim();

    IPAddress bc, ip;
    if (!bc.fromString(bcast)) { sendHTML(chatID, "❌ bcast: " + bcast); _pending.stage = PS_IDLE; return; }
    if (!ip.fromString(ipStr)) { sendHTML(chatID, "❌ ip: " + ipStr);    _pending.stage = PS_IDLE; return; }
    if (port <= 0 || port > 65535) { sendHTML(chatID, "❌ port"); _pending.stage = PS_IDLE; return; }
    if (token.length() < 8) { sendHTML(chatID, "❌ слишком короткий token"); _pending.stage = PS_IDLE; return; }

    int idx = pcm.findByName(name);
    bool isNew = (idx < 0);
    if (isNew) {
        idx = pcm.add(name, mac, bc, ip, true, /*autoSave=*/false);
        if (idx < 0) {
            sendHTML(chatID, "❌ Не удалось добавить (лимит " + String(MAX_PCS) + ")");
            _pending.stage = PS_IDLE;
            return;
        }
    } else {
        PC& pc = pcm.at(idx);
        pc.mac = mac;
        pc.broadcast = bc;
        pc.ip = ip;
        pc.hasIP = true;
    }
    pcm.setAgent(idx, (uint16_t)port, token, /*autoSave=*/false);
    pcm.save();

    logAction(userId, String(isNew ? "QR-ADD " : "QR-UPDATE ") + name);
    Serial.printf("[QR] parsed ok, idx=%d\n", idx);

    _pending.stage = PS_SEND_ADDED;
    _pending.idx = idx;
    _pending.name = name;
    _pending.isNew = isNew;
    _pending.nextAt = millis() + 200;
}

void uiTick() {
    // --- Очередь уведомлений (по 1 на тик, с паузой между отправками) ---
    if ((int32_t)(millis() - _notifyNextAt) >= 0) {
        for (int i = 0; i < NOTIFY_QUEUE_SIZE; i++) {
            if (_notifyQueue[i].used) {
                String txt = _notifyQueue[i].online
                    ? ("🟢 " + _notifyQueue[i].name + " — online")
                    : ("🔴 " + _notifyQueue[i].name + " — offline");
                Serial.printf("[NOTIFY] send: %s\n", txt.c_str());
                _bot->sendMessage(fb::Message(txt, NOTIFY_CHAT_ID));
                yield();
                _notifyQueue[i].used = false;
                _notifyQueue[i].name = "";   // освободить String
                _notifyNextAt = millis() + 1500;  // пауза между уведомлениями
                return;   // дальше не дёргаемся в том же тике
            }
        }
    }

    // --- Pending (QR-добавление) ---
    if (_pending.stage == PS_IDLE) return;
    if ((int32_t)(millis() - _pending.nextAt) < 0) return;

    if (_pending.stage == PS_PARSE) {
        doParseQR();
        return;
    }

    if (_pending.stage == PS_SEND_ADDED) {
        Serial.println(F("[UI] pending: send ADDED"));
        sendHTML(_pending.chatID,
                 String("✅ ") + (_pending.isNew ? "Добавлен" : "Обновлён") +
                 ": <b>" + _pending.name + "</b>\n"
                 "🔎 Проверяю пинг и агент...");
        _pending.stage = PS_DO_CHECK;
        _pending.nextAt = millis() + 300;
        return;
    }

    if (_pending.stage == PS_DO_CHECK) {
        _pending.stage = PS_IDLE;  // гарантия что не зациклимся
        int idx = _pending.idx;
        fb::ID chatID = _pending.chatID;
        if (idx < 0 || idx >= pcm.count()) return;
        PC& pc = pcm.at(idx);

        Serial.printf("[UI] pending check for %s\n", pc.name.c_str());
        bool alive = pingHost(pc.ip);
        Serial.printf("[UI] ping %s -> %d\n", pc.ip.toString().c_str(), (int)alive);
        AgentResult r = agentStatus(pc);
        Serial.printf("[UI] agent -> %d\n", r.httpCode);

        String res = "🏓 Ping: " + String(alive ? "🟢 online" : "🔴 offline") + "\n";
        res += "🤖 Agent: ";
        if (r.ok) {
            res += "✅ HTTP " + String(r.httpCode) + "\n<pre>" + r.body + "</pre>";
        } else {
            res += "❌ HTTP " + String(r.httpCode);
            if (r.body.length()) res += "\n<i>" + r.body + "</i>";
        }
        fb::InlineKeyboard kb;
        kb.addButton("🔎 Открыть ПК", "info_" + String(idx)).newRow();
        kb.addButton("🏠 Меню", "m_main");
        sendHTML(chatID, res, &kb);
    }
}

static void cmdAgent(fb::Update& u, const String& text) {
    fb::ID chatID = u.message().chat().id();
    char name[32], token[AGENT_TOKEN_MAX_LEN + 1];
    int port = AGENT_DEFAULT_PORT;
    int parsed = sscanf(text.c_str(), "/agent %31s %d %48s", name, &port, token);
    if (parsed < 3) {
        _bot->sendMessage(fb::Message(
            "Формат: /agent NAME PORT TOKEN\n"
            "Или отсканируйте QR из agent.py --setup",
            chatID));
        return;
    }
    int idx = pcm.findByName(String(name));
    if (idx < 0) { _bot->sendMessage(fb::Message("❌ Не найден", chatID)); return; }
    pcm.setAgent(idx, (uint16_t)port, String(token));
    logAction((int64_t)u.message().from().id(),
              "AGENT-CFG " + String(name) + " :" + String(port));
    _bot->sendMessage(fb::Message(
        "✅ Агент для " + String(name) + " настроен (порт " + String(port) + ")",
        chatID));
}

static void cmdGpio(fb::Update& u, const String& text) {
    fb::ID chatID = u.message().chat().id();
    char name[32]; int pwrPin = -1, rstPin = -1;
    int parsed = sscanf(text.c_str(), "/gpio %31s %d %d", name, &pwrPin, &rstPin);
    if (parsed < 2) {
        _bot->sendMessage(fb::Message(
            "Формат: /gpio NAME POWER_PIN [RESET_PIN]\nПример: /gpio PC1 5 4\n-1 чтобы отключить",
            chatID));
        return;
    }
    int idx = pcm.findByName(String(name));
    if (idx < 0) { _bot->sendMessage(fb::Message("❌ Не найден", chatID)); return; }
    pcm.setGpio(idx, (int8_t)pwrPin, (int8_t)rstPin);
    gpioSetupPin((int8_t)pwrPin);
    gpioSetupPin((int8_t)rstPin);
    logAction((int64_t)u.message().from().id(),
              "GPIO " + String(name) + " pwr=" + String(pwrPin) + " rst=" + String(rstPin));
    _bot->sendMessage(fb::Message(
        "✅ " + String(name) + ": power=" + String(pwrPin) + " reset=" + String(rstPin),
        chatID));
}

void uiHandleMessage(fb::Update& u) {
    Text txt = u.message().text();
    fb::ID chatID = u.message().chat().id();

    // QR-пейлоад (автопарсинг)
    String txtStr = String(txt);
    if (tryHandleQRPayload(u, txtStr)) return;

    if (txt == "/start" || txt == "/menu") {
        uiShowMainMenu(chatID);
        return;
    }
    if (txt.startsWith("/add "))   { cmdAdd(u, txtStr);   return; }
    if (txt.startsWith("/group ")) { cmdGroup(u, txtStr); return; }
    if (txt.startsWith("/gpio "))  { cmdGpio(u, txtStr);  return; }
    if (txt.startsWith("/agent ")) { cmdAgent(u, txtStr); return; }

    if (txt == "/status" || txt == "/stat") {
        fb::Message m;
        m.text = "📊 Uptime: " + fmtUptime(millis()) +
                 "\nFree heap: " + fmtBytes(ESP.getFreeHeap()) +
                 "\nRSSI: " + String(WiFi.RSSI()) + " dBm";
        m.chatID = chatID;
        _bot->sendMessage(m);
        return;
    }
    if (txt == "/help") {
        _bot->sendMessage(fb::Message(
            "Команды:\n"
            "/start /menu — меню\n"
            "/add NAME MAC BCAST [IP]\n"
            "/group NAME GROUP (- чтобы убрать)\n"
            "/gpio NAME POWER_PIN RESET_PIN\n"
            "/agent NAME PORT TOKEN\n"
            "/status — состояние бота\n\n"
            "Можно прислать текстом QR-строку WOLBOT|... — ПК\n"
            "добавится автоматически вместе с агентом.",
            chatID));
        return;
    }
}

// =========================================================
// Notifications from monitor
// =========================================================

void uiNotifyStatus(const String& name, bool online) {
#if NOTIFY_ON_STATUS_CHANGE
    // НЕ шлём прямо отсюда — блокировка sendMessage вкупе с pingHost() даёт WDT.
    // Ставим в очередь, uiTick() отправит в безопасном окне.
    for (int i = 0; i < NOTIFY_QUEUE_SIZE; i++) {
        if (!_notifyQueue[i].used) {
            _notifyQueue[i].used   = true;
            _notifyQueue[i].name   = name;
            _notifyQueue[i].online = online;
            return;
        }
    }
    // Очередь полна — молча игнорим (не критично для уведомлений)
    Serial.println(F("[NOTIFY] queue full, dropping"));
#endif
}
