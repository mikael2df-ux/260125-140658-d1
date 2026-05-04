# ESP8266 WOL Telegram Bot

Telegram-бот для удалённого управления компьютерами через Wake-on-LAN, мониторинг их статуса и физическое управление кнопками питания/сброса.

## Возможности

- **Wake-on-LAN**: Удалённое включение компьютеров по сети
- **Мониторинг статуса**: Автоматическая проверка доступности ПК через ICMP ping
- **Уведомления**: Оповещения в Telegram при изменении статуса (online/offline)
- **Физическое управление**: Управление кнопками Power/Reset через GPIO (реле/оптопары)
- **HTTP Agent**: Интеграция с агентом на ПК для расширенных команд (shutdown, reboot, sleep, lock)
- **Группировка**: Организация ПК по группам
- **История и статистика**: Runtime-график uptime и счётчики включений с момента запуска ESP
- **Безопасность**: Whitelist администраторов, rate limiting, логирование действий

## Аппаратная часть

- **Микроконтроллер**: ESP8266 (WeMos D1)
- **Опционально**: Реле или оптопары для управления кнопками Power/Reset

## Требования

### Программное обеспечение
- [PlatformIO IDE](https://platformio.org/platformio-ide) для Visual Studio Code
- Arduino framework для ESP8266

### Библиотеки
Устанавливаются автоматически через PlatformIO:
- `FastBot2` - Telegram Bot API
- `ArduinoJson` v6 - JSON парсинг
- `ESP8266Ping` - ICMP ping

## Установка и настройка

### 1. Клонирование проекта
```bash
git clone <repository-url>
cd esp8266-wol-telegram-bot
```

### 2. Настройка секретов

Создайте файл `src/secrets.h` со следующим содержимым:

```cpp
#pragma once

// Wi-Fi
#define WIFI_SSID "your_wifi_ssid"
#define WIFI_PASS "your_wifi_password"

// Telegram Bot Token (получить у @BotFather)
#define BOT_TOKEN "1234567890:ABCdefGHIjklMNOpqrsTUVwxyz"

// ID администраторов (получить у @userinfobot)
#define ADMIN_IDS { 123456789LL, 987654321LL }

// Chat ID для уведомлений
#define NOTIFY_CHAT_ID 123456789LL
```

### 3. Настройка порта загрузки

Отредактируйте `platformio.ini` и укажите ваш COM-порт:

```ini
upload_port = COM8      ; Измените на ваш порт
monitor_port = COM8
```

### 4. Сборка и загрузка

Через Visual Studio Code с расширением PlatformIO IDE:
- откройте папку проекта;
- выберите окружение `d1`;
- используйте команды PlatformIO: Build, Upload, Monitor.

Через CLI, если `pio` добавлен в PATH:

```bash
pio run -t upload
pio device monitor
```

## Использование

### Команды бота

- `/start` или `/menu` - Главное меню
- `/help` - Справка по командам
- `/status` или `/stat` - Статус ESP-бота
- `/add NAME MAC BCAST [IP]` - Добавить новый ПК
- `/group NAME GROUP` - Назначить группу (`-` чтобы убрать)
- `/gpio NAME POWER_PIN [RESET_PIN]` - Настроить GPIO для физического управления
- `/agent NAME PORT TOKEN` - Настроить HTTP Agent вручную
- Inline-кнопки для управления ПК:
  - Wake (WOL)
  - Status агента
  - Power/Reset (если настроены GPIO)
  - Shutdown/Reboot/Sleep/Lock (если настроен агент)

### Добавление ПК

Отправьте боту команду:

```text
/add PC1 AA:BB:CC:DD:EE:FF 192.168.1.255 192.168.1.10
```

Где:
- `PC1` - имя ПК
- `AA:BB:CC:DD:EE:FF` - MAC-адрес
- `192.168.1.255` - broadcast адрес
- `192.168.1.10` - IP-адрес для мониторинга (опционально)

Группу, GPIO и агент можно настроить отдельными командами `/group`, `/gpio`, `/agent`.

### Настройка HTTP Agent

Для расширенных команд (shutdown, reboot) установите агент на целевой ПК:
1. Перейдите в папку `agent/`
2. Установите зависимости из `requirements.txt`
3. Запустите `python agent.py --setup`
4. Скопируйте текстовый payload вида `WOLBOT|v1|...` из вывода агента и отправьте его боту
5. Бот добавит или обновит ПК, сохранит token/port, проверит ping и `/status` агента

Подробности есть в `agent/README.md`.

## Конфигурация

Основные параметры в `src/config.h`:

```cpp
#define MAX_PCS               8      // Максимум ПК
#define MONITOR_INTERVAL_MS   10000  // Интервал проверки (мс)
#define PING_TIMEOUT_MS       800    // Таймаут ping
#define RATE_LIMIT_PER_MIN    30     // Лимит команд/мин
```

Данные ПК сохраняются в LittleFS-файл `/pcs.json`. Runtime-статистика, история uptime и лог действий хранятся в RAM и сбрасываются после перезагрузки ESP.

## Структура проекта

```
src/
├── main.cpp           # Основной файл, setup/loop
├── config.h           # Конфигурация
├── secrets.h          # Секреты (не в git)
├── pc_manager.*       # Управление списком ПК
├── bot_ui.*           # Telegram UI и обработка команд
├── monitor.*          # Мониторинг статуса ПК
├── wol.*              # Wake-on-LAN
├── gpio_ctrl.*        # Управление GPIO
├── agent_client.*     # HTTP клиент для агента
├── network_utils.*    # Сетевые утилиты
└── security.*         # Rate limiting, логирование
```

## Безопасность

- Только пользователи из `ADMIN_IDS` могут управлять ботом
- Rate limiting: максимум 30 команд в минуту на пользователя
- Подтверждение для деструктивных операций (удаление, hard reset)
- Логирование всех действий

## Troubleshooting

### Бот не отвечает
- Проверьте подключение к Wi-Fi
- Убедитесь, что токен бота корректен
- Проверьте, что ваш ID в списке `ADMIN_IDS`

### WOL не работает
- Убедитесь, что WOL включён в BIOS целевого ПК
- Проверьте правильность MAC-адреса и broadcast адреса
- Убедитесь, что ПК и ESP8266 в одной подсети

### Мониторинг показывает offline
- Проверьте, что IP-адрес ПК корректен
- Убедитесь, что ICMP ping не блокируется файрволом
- Увеличьте `PING_TIMEOUT_MS` если сеть медленная

## Лицензия

Проект использует библиотеки с открытым исходным кодом. Проверьте лицензии зависимостей.

## Автор

Проект создан для управления домашними/офисными компьютерами через Telegram.
