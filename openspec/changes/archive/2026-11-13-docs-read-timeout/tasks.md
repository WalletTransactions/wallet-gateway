# Tasks

## 1. Таймаут маршрута

- [x] 1.1 Добавить в `RouteTarget` таймаут чтения (по умолчанию 30 с), для `/api/docs/*` — `docs_read_timeout_sec` из `WALLET_DOCS_READ_TIMEOUT_SEC` (по умолчанию 130); `proxy_request` берёт таймаут из маршрута. Проверка: сборка образа; апстрим docs, отвечающий через 40 с, доходит до клиента `200`; такой же апстрим на `/api/ai/*` даёт `502` через 30 с.
- [x] 1.2 Неверное значение `WALLET_DOCS_READ_TIMEOUT_SEC` (`abc`, `0`, `-5`, `10s`) — ошибка в журнале и выход с кодом `1`; без переменной — 130 с в журнале старта. Проверка: запуск контейнера с каждым значением.

## 2. Документация

- [x] 2.1 README (переменные окружения) и `.env.example`: `WALLET_DOCS_READ_TIMEOUT_SEC` и правило «длиннее `LLM_TIMEOUT_MS` wallet-docs». Проверка: описание совпадает с поведением.

## Заметки к выкату

- 2026-11-13: образ собран (`docker build --memory=3g --build-arg BUILD_JOBS=3`), VERSION 0.1.5. Проверка — контейнер гейтвея с JWKS/токенами `wallet-docs/scripts/it_stub.py` и апстримом-заглушкой, отвечающим через 40 с:
  - без переменной: в журнале старта `ожидание ответа 130 с`; `POST /api/docs/ask` — `200` через 40 с; `GET /api/reports/…` с тем же апстримом — `502` через 30 с; быстрые `/api/reports/accounts` и `/api/docs/status` — `200`;
  - `WALLET_DOCS_READ_TIMEOUT_SEC=35`: `/api/docs/ask` — `502` через 35 с и строка `upstream … недоступен (Failed to read connection)`;
  - `abc`, `0`, `-5`, `10s`: ошибка `WALLET_DOCS_READ_TIMEOUT_SEC must be a positive integer` и код `1`.

## Workflow follow-up

- Выкатить на test; убрать временный `LLM_TIMEOUT_MS=25000` из `wallet-docs.env` test (вернуть умолчание 120 с); проверить вопрос на экране «Справка», затем архивировать.
