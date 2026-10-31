# Tasks

## 1. Адрес апстрима

- [ ] 1.1 Добавить `wallet_mcp_url` в `AppState` и прочитать `WALLET_MCP_URL` через `env_required` с подсказкой `адрес wallet-mcp, например http://127.0.0.1:8088`, отбросив завершающие `/`. Проверка: запуск без переменной пишет в журнал `WALLET_MCP_URL must be set — адрес wallet-mcp, например http://127.0.0.1:8088 (см. README.md)` и завершается с кодом 1.
- [ ] 1.2 Добавить `WALLET_MCP_URL=http://127.0.0.1:8088` с комментарием в `.env.example`, описание переменной в `README.md`. Проверка: гейтвей стартует с `.env`, скопированным из `.env.example`.

## 2. Маршрут без гейта

- [ ] 2.1 В `find_route` добавить ветку `/api/mcp/` → `wallet_mcp_url` с `require_jwt = false`, пустым `audience` и меткой `proxy /api/mcp/*` — после `/api/ai/` и до общего `/api/`. Обновить комментарий над `find_route` про незащищённые префиксы. Проверка: `curl -si -X POST http://<хост>:8090/api/mcp/` без токена возвращает ответ wallet-mcp (`401` wallet-mcp, а не текст `gateway: access-токен отсутствует…`).
- [ ] 2.2 Проверить, что заголовки доходят как есть: `curl -si -X POST -H 'Authorization: Bearer wmcp_test' http://<хост>:8090/api/mcp/`. Проверка: в журнале wallet-mcp запрос с тем же `trace_id`, а `jwt_claims_total` гейтвея на `/metrics` не вырос.
- [ ] 2.3 Проверить, что соседние маршруты не сдвинулись: `GET /api/reports/accounts` и `GET /api/currency-pairs` без токена получают `401` гейтвея, `POST /api/mcp` (без слеша) без токена тоже получает `401` гейтвея. Проверка: три curl с ожидаемыми ответами.
- [ ] 2.4 `openapi.yaml`: путь `/api/mcp/{proxyPath}` (метод `post`, без `gatedProxy`, `security: []`, ссылка на контракт wallet-mcp), описание незакрытых префиксов в теге `gated-proxy`. `README.md`: строка `/api/mcp/* ──> wallet-mcp (MCP, без JWT-гейта, :8088)` в схеме маршрутов и абзац, почему гейт здесь не применяется. Проверка: `/docs` гейтвея открывается, новый путь виден в спеке, yaml валиден.

## 3. Интеграция на стенде

- [ ] 3.1 На test-контуре с wallet-mcp и персональным токеном пройти `initialize` и `tools/list` через `http://<хост>:8091/api/mcp/`. Проверка: curl возвращает список инструментов, а в журнале гейтвея у запроса метка маршрута `proxy /api/mcp/*`.
- [ ] 3.2 Остановить wallet-mcp и повторить `POST /api/mcp/`. Проверка: ответ `502` с телом `gateway: сервис недоступен`, в журнале `upstream http://wallet-mcp:8088 недоступен (…) для POST /api/mcp/`.

## Workflow follow-up

- Выкатывать после wallet-mcp и после того, как `WALLET_MCP_URL` появится в окружении контура (wallet-infra `add-wallet-mcp-service`).
- Архивировать change после выката в prod.
