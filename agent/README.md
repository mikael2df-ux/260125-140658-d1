# WOL-Bot PC Agent

Маленький локальный HTTP-сервис, который слушает команды от Telegram-бота
(ESP8266, `FastBot2`) и выполняет soft-операции на ПК: **shutdown / reboot /
sleep / lock**, а также отдаёт `GET /status` с базовой инфой о системе.

## Установка

```bash
# В папке agent/
python -m venv .venv
# Windows:
.venv\Scripts\activate
# Linux / macOS:
source .venv/bin/activate

pip install -r requirements.txt
```

Зависимости:
- `qrcode[pil]` — для генерации QR при setup
- `psutil` — необязательно, даёт CPU/RAM в `/status`

Без этих пакетов агент тоже запустится, но `/setup` не нарисует QR, а в
`/status` не будет `cpu`/`ram`.

## Первичная настройка

На каждом ПК:

```bash
python agent.py --setup
```

Что произойдёт:
1. Агент определит: `hostname`, MAC, IP, broadcast.
2. Сгенерирует случайный `token` (32 hex-символа).
3. Сохранит `agent.json` в этой же папке.
4. Покажет **QR-код в терминале** (ASCII) и PNG-файл `agent_qr.png`.
5. Покажет текстовый «пейлоад» вида:
   ```
   WOLBOT|v1|MyPC|AA:BB:CC:DD:EE:FF|192.168.1.255|192.168.1.50|8765|ab12...cd34
   ```

## Добавление ПК в бота

**Способ 1 — QR (одним движением):**
1. Сканируй QR любым сканером (камера iOS/Android, Google Lens, TG-сканер).
2. Скопируй получившийся текст.
3. Отправь боту сообщением в чат.
4. Бот автоматически:
   - добавит ПК,
   - сохранит token и порт агента,
   - пингнёт IP,
   - проверит `/status` агента,
   - пришлёт результат с кнопкой открыть карточку ПК.

**Способ 2 — вручную:**
```
/add MyPC AA:BB:CC:DD:EE:FF 192.168.1.255 192.168.1.50
/agent MyPC 8765 ab12...cd34
```

## Запуск агента

```bash
python agent.py
```

Агент будет слушать на `0.0.0.0:<port из agent.json>`. Порт по умолчанию — `8765`.

### Автозапуск

**Windows (Task Scheduler):**
```
schtasks /create /tn "WOL-Bot Agent" /tr "pythonw C:\path\to\agent\agent.py" ^
  /sc onstart /ru SYSTEM
```

**Linux (systemd), пример `/etc/systemd/system/wolbot-agent.service`:**
```ini
[Unit]
Description=WOL-Bot PC Agent
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /opt/wolbot/agent.py
WorkingDirectory=/opt/wolbot
Restart=on-failure
User=root

[Install]
WantedBy=multi-user.target
```
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now wolbot-agent
```

## API

| Метод | Путь         | Auth            | Описание                       |
|-------|--------------|-----------------|--------------------------------|
| GET   | `/health`    | —               | живой ли сервис                |
| GET   | `/status`    | `Bearer <tok>`  | hostname, uptime, cpu, ram, ip |
| POST  | `/shutdown`  | `Bearer <tok>`  | выключение ПК                  |
| POST  | `/reboot`    | `Bearer <tok>`  | перезагрузка                   |
| POST  | `/sleep`     | `Bearer <tok>`  | сон / suspend                  |
| POST  | `/lock`      | `Bearer <tok>`  | заблокировать сеанс            |

Пример ответа `/status`:
```json
{
  "host":   "MyPC",
  "os":     "Windows-11-10.0.22631",
  "uptime": 12345,
  "cpu":    7.3,
  "ram":    52.1,
  "ip":     "192.168.1.50",
  "agent":  "1.0"
}
```

## Безопасность

- Агент принимает команды только с правильным Bearer-токеном.
- Токен генерируется `secrets.token_hex(16)` при `--setup` и хранится в
  `agent.json` — **защищай этот файл (600)**.
- Используй агент только в доверенной локальной сети. Для доступа извне
  прокидывай через VPN (Tailscale / WireGuard), а не ставь ESP+агент в
  открытый интернет с одним лишь токеном.
- Для ещё большей паранойи — ограничь источник по IP на уровне файрвола ПК
  (разрешить только IP твоей ESP).

## Траблшутинг

- **`/shutdown` не работает на Linux** — нужен `sudo` / запуск агента от
  root, либо `polkit` rule для `systemctl poweroff`.
- **QR не появляется** — установи `qrcode[pil]` или используй текстовый
  пейлоад вручную через `/agent`.
- **CPU/RAM = null** — поставь `psutil`.
- **Бот не достаёт агент** — проверь `Windows Defender Firewall` (открыть
  порт 8765) и что ESP и ПК в одной подсети.
