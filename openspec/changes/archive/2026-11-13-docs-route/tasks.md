# Tasks

## 1. Маршрут и окружение

- [x] 1.1 Добавить в чтение окружения необязательный `WALLET_DOCS_URL` (без завершающих `/`, пустое значение = «не задан»). Проверка: гейтвей стартует без переменной и с ней; в журнале нет `WALLET_DOCS_URL must be set`.
- [x] 1.2 Добавить префикс `/api/docs/` в таблицу маршрутов перед `/api/mcp/` с адресатом `aud` `wallet-docs`. Проверка: на живом стеке `curl -i` на `/api/docs/status` с токеном без `wallet-docs` в `aud` даёт `401`, а с токеном, где он есть, запрос доходит до апстрима (при заданном URL).
- [x] 1.3 Ответ `404` с телом `gateway: docs не подключён` при пустом `WALLET_DOCS_URL`, до проверки токена. Проверка: `curl -i` без токена на `/api/docs/status` — `404` и точный текст; `curl -i` на `/api/docs` без слеша уходит в wallet-api, а не в docs.
- [x] 1.4 Проверить `502` при заданном, но остановленном апстриме и отсутствие регрессий остальных маршрутов. Проверка: `curl` на `/api/reports/…`, `/api/ai/status`, `/api/mcp/` и `/sync/…` отвечает как до change; остановленный wallet-docs даёт `502 gateway: сервис недоступен`.

## 2. Документация

- [x] 2.1 Обновить `openapi.yaml` (строка про `/api/docs/*` со ссылкой на контракт wallet-docs) и `README.md` (таблица маршрутов, необязательность `WALLET_DOCS_URL`). Проверка: Swagger UI гейтвея на `/docs` открывается и содержит новый префикс; команды из README выполняются как написано.

## Заметки к выкату

- 2026-11-13: образ собран (`docker build --memory=3g --build-arg BUILD_JOBS=3`), VERSION 0.1.4. Проверка — контейнер гейтвея с заглушкой JWKS/токенов (`wallet-docs/scripts/it_stub.py`) и эхо-апстримами вместо wallet-docs и wallet-api:
  - без `WALLET_DOCS_URL`: старт без `WALLET_DOCS_URL must be set`, в журнале старта `wallet-docs: не подключён`; `GET /api/docs/status` без токена и с токеном — `404 gateway: docs не подключён`; `/api/docs` без слеша и `/api/reports/accounts` уходят как раньше;
  - с `WALLET_DOCS_URL`: без токена и с токеном без `wallet-docs` в `aud` — `401`; с ним `GET /api/docs/status` и `POST /api/docs/ask` доходят до апстрима docs тем же путём; `/api/docs` — в wallet-api; `/api/mcp/` — в свой апстрим без гейта; `/sync/push` без токена — `401`;
  - адрес задан, апстрим не слушает — `502 gateway: сервис недоступен`.
- Проверка на живом test-контуре — после выката google-auth `docs-audience` и wallet-docs (D4).

## Workflow follow-up

- Выкатить после google-auth `docs-audience` и сервиса wallet-docs; затем задать `WALLET_DOCS_URL` в `infra/env/<env>.env` вместе с профилем `docs` (wallet-infra, change `wallet-docs-assistant`).
- Архивировать change после выката на test и проверки.
