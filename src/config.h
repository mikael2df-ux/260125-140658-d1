#pragma once

// ---------- ЛИМИТЫ ----------
#define MAX_PCS               8
#define MAX_NAME_LEN          20

// ---------- ФАЙЛЫ ----------
#define PCS_FILE              "/pcs.json"
#define STATS_FILE            "/stats.json"

// ---------- WOL / СЕТЬ ----------
#define WOL_PORT              9

// ---------- МОНИТОРИНГ ----------
// Период «опроса» одного ПК. Внутри цикла пингуется по одному ПК за раз.
// Полный круг = INTERVAL * N_pcs. Слишком короткий → WDT + загружает сеть.
#define MONITOR_INTERVAL_MS   10000UL
// Сколько промахов подряд, чтобы считать «ПК выключился»
#define MONITOR_OFFLINE_HITS  3
// Таймаут одного ICMP-пинга
#define PING_TIMEOUT_MS       800
// Включить уведомления при смене статуса ПК
#define NOTIFY_ON_STATUS_CHANGE  1

// ---------- ИСТОРИЯ / ГРАФИК ----------
#define HISTORY_POINTS        48           // точек на график
#define HISTORY_INTERVAL_MS   300000UL     // 5 мин * 48 = 4 часа глубины
#define CHART_WIDTH           48
#define CHART_HEIGHT          8

// ---------- ГРУППЫ ----------
#define MAX_GROUP_LEN         16           // макс длина имени группы

// ---------- ФИЗИЧЕСКОЕ УПРАВЛЕНИЕ (GPIO) ----------
// Для Proxmox-ноута: реле/оптопара на кнопки Power и Reset.
// -1 = пин не задан / не использовать
#define GPIO_POWER_PULSE_MS   500          // короткий импульс Power (включение)
#define GPIO_POWER_LONG_MS    5000         // долгое удержание Power (принудит. выкл)
#define GPIO_RESET_PULSE_MS   200          // импульс Reset
#define GPIO_ACTIVE_LEVEL     LOW          // уровень, подающий сигнал на реле (обычно LOW для opto)
#define GPIO_IDLE_LEVEL       HIGH

// ---------- HTTP AGENT ----------
#define AGENT_DEFAULT_PORT    8765
#define AGENT_TIMEOUT_MS      4000
#define AGENT_TOKEN_MAX_LEN   48           // ожидаем ~32 hex символа
#define QR_PREFIX             "WOLBOT|v1|" // магия для парсинга QR-сообщения

// ---------- БЕЗОПАСНОСТЬ ----------
#define RATE_LIMIT_PER_MIN    30           // макс команд в минуту на юзера
#define ACTION_LOG_SIZE       20           // сколько последних действий хранить
#define CONFIRM_DESTRUCTIVE   1            // спрашивать подтверждение на удаление / hard reset

// ---------- БОТ ----------
#define BOT_POLL_PERIOD_MS    60000UL   // long polling
// Запретить боту реагировать первые N мс после загрузки (устаревшие команды)
#define BOT_STARTUP_DELAY_MS  2000UL
