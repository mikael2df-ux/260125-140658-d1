#pragma once
#include <FastBot2.h>

// Инициализация — сохраняем указатель на бота
void uiInit(FastBot2* bot);

// Показать главное меню новым сообщением (на /start)
void uiShowMainMenu(fb::ID chatID);

// Обработка query-коллбэка (нажатие inline-кнопки)
void uiHandleQuery(fb::Update& u);

// Обработка текстовых сообщений (/start, /add и т.п.)
void uiHandleMessage(fb::Update& u);

// Уведомить о смене статуса ПК (вызывается из monitor callback)
void uiNotifyStatus(const String& name, bool online);

// Периодический тик UI — обрабатывает отложенные действия
// (например post-QR проверку ping/agent в безопасном контексте).
void uiTick();
