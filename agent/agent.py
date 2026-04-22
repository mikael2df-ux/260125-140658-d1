#!/usr/bin/env python3
"""
WOL-Bot PC Agent.

HTTP-сервис для удалённого soft-управления ПК из Telegram-бота на ESP8266.

Использование:
    python agent.py --setup    # интерактивная настройка, генерация QR
    python agent.py            # запуск HTTP-сервера (использует agent.json)

Endpoints (все, кроме /status, требуют Bearer-auth):
    GET  /status     — JSON с инфо о ПК (cpu, ram, uptime, hostname)
    POST /shutdown   — выключить ПК
    POST /reboot     — перезагрузка
    POST /sleep      — уйти в сон
    POST /lock       — заблокировать сеанс
"""
import argparse
import ctypes
import json
import os
import platform
import secrets
import socket
import subprocess
import sys
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CONFIG_PATH = Path(__file__).resolve().parent / "agent.json"
DEFAULT_PORT = 8765
STARTED_AT = time.time()

# ---------------------------------------------------------------------------
# Системные утилиты
# ---------------------------------------------------------------------------

IS_WIN   = platform.system().lower() == "windows"
IS_LINUX = platform.system().lower() == "linux"
IS_MAC   = platform.system().lower() == "darwin"


def get_hostname() -> str:
    return socket.gethostname()


def get_mac() -> str:
    """Возвращает MAC в формате AA:BB:CC:DD:EE:FF (активный интерфейс)."""
    try:
        node = uuid.getnode()
        if (node >> 40) & 0x01:
            # Случайный MAC — значит uuid.getnode не нашёл реальный.
            raise RuntimeError("random MAC")
        mac = ":".join("%02X" % ((node >> i) & 0xFF) for i in range(40, -1, -8))
        return mac
    except Exception:
        return "00:00:00:00:00:00"


def get_primary_ip() -> str:
    """IP, который используется для исходящих соединений (не loopback)."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def get_broadcast(ip: str) -> str:
    """Грубо: /24 → последний октет = 255. Для типовых домашних сетей ок."""
    parts = ip.split(".")
    if len(parts) == 4:
        parts[3] = "255"
        return ".".join(parts)
    return "255.255.255.255"


def get_uptime_seconds() -> int:
    """Системный uptime (не uptime агента)."""
    try:
        if IS_LINUX:
            with open("/proc/uptime") as f:
                return int(float(f.read().split()[0]))
        if IS_WIN:
            return int(ctypes.windll.kernel32.GetTickCount64() / 1000)
        if IS_MAC:
            out = subprocess.check_output(["sysctl", "-n", "kern.boottime"]).decode()
            # kern.boottime: { sec = 12345678, usec = ... }
            sec = int(out.split("sec = ")[1].split(",")[0])
            return int(time.time() - sec)
    except Exception:
        pass
    return int(time.time() - STARTED_AT)


def get_cpu_ram():
    """Возвращает (cpu_percent, ram_percent) или (None, None)."""
    try:
        import psutil  # optional
        return psutil.cpu_percent(interval=0.2), psutil.virtual_memory().percent
    except ImportError:
        return None, None


# ---------------------------------------------------------------------------
# Power actions
# ---------------------------------------------------------------------------

def action_shutdown():
    if IS_WIN:
        subprocess.run(["shutdown", "/s", "/t", "5", "/f"], check=False)
    elif IS_LINUX:
        subprocess.run(["systemctl", "poweroff"], check=False)
    elif IS_MAC:
        subprocess.run(["osascript", "-e", 'tell app "System Events" to shut down'], check=False)


def action_reboot():
    if IS_WIN:
        subprocess.run(["shutdown", "/r", "/t", "5", "/f"], check=False)
    elif IS_LINUX:
        subprocess.run(["systemctl", "reboot"], check=False)
    elif IS_MAC:
        subprocess.run(["osascript", "-e", 'tell app "System Events" to restart'], check=False)


def action_sleep():
    if IS_WIN:
        # rundll32 PowrProf.dll,SetSuspendState 0,1,0 (включает hibernate если доступно)
        subprocess.run(
            ["rundll32.exe", "powrprof.dll,SetSuspendState", "0,1,0"], check=False
        )
    elif IS_LINUX:
        subprocess.run(["systemctl", "suspend"], check=False)
    elif IS_MAC:
        subprocess.run(["pmset", "sleepnow"], check=False)


def action_lock():
    if IS_WIN:
        ctypes.windll.user32.LockWorkStation()
    elif IS_LINUX:
        # Пробуем несколько вариантов
        for cmd in (
            ["loginctl", "lock-session"],
            ["xdg-screensaver", "lock"],
            ["gnome-screensaver-command", "-l"],
        ):
            if subprocess.run(cmd, check=False).returncode == 0:
                return
    elif IS_MAC:
        subprocess.run(
            ["pmset", "displaysleepnow"], check=False
        )  # проще всего — усыпить дисплей


ACTIONS = {
    "shutdown": action_shutdown,
    "reboot":   action_reboot,
    "sleep":    action_sleep,
    "lock":     action_lock,
}


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------

class Handler(BaseHTTPRequestHandler):
    server_version = "WOLBotAgent/1.0"
    cfg = None  # заполняется в main()

    # Reduce default logging spam
    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s - %s\n" % (self.log_date_time_string(),
                                             self.address_string(), fmt % args))

    def _auth_ok(self) -> bool:
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer "):
            return False
        return secrets.compare_digest(auth[7:].strip(), self.cfg["token"])

    def _reply(self, code: int, body: dict | str):
        data = body if isinstance(body, (bytes, str)) else json.dumps(body)
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _status_payload(self) -> dict:
        cpu, ram = get_cpu_ram()
        return {
            "host":   get_hostname(),
            "os":     platform.platform(),
            "uptime": get_uptime_seconds(),
            "cpu":    cpu,
            "ram":    ram,
            "ip":     get_primary_ip(),
            "agent":  "1.0",
        }

    def do_GET(self):
        if self.path in ("/", "/health"):
            return self._reply(200, {"ok": True})
        if self.path == "/status":
            if not self._auth_ok():
                return self._reply(401, {"error": "unauthorized"})
            return self._reply(200, self._status_payload())
        return self._reply(404, {"error": "not found"})

    def do_POST(self):
        if not self._auth_ok():
            return self._reply(401, {"error": "unauthorized"})
        action = self.path.lstrip("/")
        if action not in ACTIONS:
            return self._reply(404, {"error": f"unknown action '{action}'"})
        try:
            ACTIONS[action]()
        except Exception as e:
            return self._reply(500, {"error": str(e)})
        return self._reply(200, {"ok": True, "action": action})


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

def generate_token(nbytes: int = 16) -> str:
    return secrets.token_hex(nbytes)  # 32 hex символов для nbytes=16


def qr_ascii(text: str) -> str:
    """Рисует QR в stdout и возвращает сохранённый путь PNG (если Pillow есть).

    На Windows с cp1251/cp866 юникод-блоки print_ascii ломаются. Используем
    свой рендер через get_matrix() чистыми ASCII-символами ('##' и '  ').
    """
    try:
        import qrcode
    except ImportError:
        print("\n[!] Модуль 'qrcode' не установлен. Установи: pip install qrcode[pil]")
        print("[!] QR-код не будет сгенерирован, но текстовый пейлоад (ниже)")
        print("    можно отправить боту вручную как сообщение.")
        return ""

    qr = qrcode.QRCode(border=2, box_size=1)
    qr.add_data(text)
    qr.make(fit=True)

    # Свой ASCII-рендер (устойчив к любой кодировке терминала)
    matrix = qr.get_matrix()
    border = " " * 2
    print()
    print(border + "  " * (len(matrix[0]) + 2))
    for row in matrix:
        line = border + "  "
        for cell in row:
            line += "##" if cell else "  "
        line += "  "
        print(line)
    print(border + "  " * (len(matrix[0]) + 2))
    print()

    # PNG, если есть PIL
    png_path = Path(__file__).resolve().parent / "agent_qr.png"
    try:
        img = qr.make_image(fill_color="black", back_color="white")
        img.save(png_path)
        return str(png_path)
    except Exception:
        return ""


def setup_interactive(port: int):
    print("=" * 60)
    print(" WOL-Bot Agent — Setup")
    print("=" * 60)

    hostname = get_hostname()
    mac = get_mac()
    ip = get_primary_ip()
    bcast = get_broadcast(ip)
    token = generate_token()

    print(f"  Host:       {hostname}")
    print(f"  MAC:        {mac}")
    print(f"  IP:         {ip}")
    print(f"  Broadcast:  {bcast}")
    print(f"  Port:       {port}")
    print(f"  Token:      {token}")
    print()
    # ПК-имя можно переопределить
    custom = input(f"  Имя ПК для бота [{hostname}]: ").strip()
    if custom:
        hostname = custom

    payload = f"WOLBOT|v1|{hostname}|{mac}|{bcast}|{ip}|{port}|{token}"

    cfg = {
        "name":  hostname,
        "mac":   mac,
        "ip":    ip,
        "bcast": bcast,
        "port":  port,
        "token": token,
    }
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2))
    print(f"\n[+] Конфиг записан: {CONFIG_PATH}")

    print("\n[+] QR-код для отправки в Telegram-бота:\n")
    png = qr_ascii(payload)
    print(f"\nПолезная нагрузка (на случай если QR не отсканируется):\n")
    print("  " + payload + "\n")
    if png:
        print(f"[+] PNG сохранён: {png}\n")

    print("Что делать дальше:")
    print("  1. Отсканируй QR с экрана любым сканером на телефоне")
    print("  2. Скопируй полученный текст и отправь боту сообщением")
    print("     (или вручную введи команду /agent NAME PORT TOKEN)")
    print("  3. Бот автоматически добавит ПК, проверит ping и агент")
    print("  4. Запусти агент: python agent.py")


def run_server():
    if not CONFIG_PATH.exists():
        print(f"[!] Нет файла {CONFIG_PATH}. Запусти сначала: python agent.py --setup")
        sys.exit(1)
    cfg = json.loads(CONFIG_PATH.read_text())
    Handler.cfg = cfg
    bind = ("0.0.0.0", int(cfg["port"]))
    print(f"[+] WOL-Bot Agent listening on http://{bind[0]}:{bind[1]}")
    print(f"    Host: {cfg.get('name')}  IP: {cfg.get('ip')}")
    try:
        with ThreadingHTTPServer(bind, Handler) as srv:
            srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[+] Stopped")


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="WOL-Bot PC Agent")
    ap.add_argument("--setup", action="store_true", help="interactive setup + QR")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT,
                    help=f"HTTP port (default {DEFAULT_PORT})")
    args = ap.parse_args()
    if args.setup:
        setup_interactive(args.port)
    else:
        run_server()


if __name__ == "__main__":
    main()
