## MODIFIED Requirements

### Requirement: Маршруты API
Пути API SHALL уходить в такие апстримы. Все, кроме `/api/mcp/`, проходят
JWT-гейт с указанным адресатом `aud`:

| Префикс | Апстрим | aud |
| --- | --- | --- |
| `/api/reports/` | wallet-report (`WALLET_REPORT_API_URL`) | `wallet-report` |
| `/api/ai/` | wallet-ai (`WALLET_AI_URL`) | `wallet-ai` |
| `/api/mcp/` | wallet-mcp (`WALLET_MCP_URL`) | — (без гейта, см. `edge-jwt-gate`) |
| `/api/` (прочее) | wallet-api (`WALLET_API_URL`) | `wallet-api` |
| `/sync/` | wallet-api (`WALLET_API_URL`) | `wallet-api` |

Путь, query и тело уходят апстриму без изменений. Контракты путей — в
`openapi.yaml` соответствующего апстрима.

#### Scenario: Отчёты раньше общего /api/
- **WHEN** приходит `GET /api/reports/accounts`
- **THEN** запрос уходит в wallet-report, а не в wallet-api

#### Scenario: ИИ раньше общего /api/
- **WHEN** приходит `DELETE /api/ai/jobs/{id}`
- **THEN** запрос уходит в wallet-ai

#### Scenario: MCP раньше общего /api/
- **WHEN** приходит `POST /api/mcp/` с заголовком `Authorization: Bearer wmcp_…`
- **THEN** запрос уходит в wallet-mcp на тот же путь `/api/mcp/` с тем же заголовком, а не в wallet-api

#### Scenario: Синхронизация
- **WHEN** приходит `POST /sync/push`
- **THEN** запрос уходит в wallet-api

### Requirement: Порядок и сравнение префиксов
Гейтвей SHALL сравнивать префиксы с путём запроса (`req.path`, без query) в
порядке `/api/reports/`, `/api/ai/`, `/api/mcp/`, `/api/`, `/sync/`, `/auth/`,
`/admin/`; первый совпавший побеждает. Префикс сравнивается вместе с
завершающим `/`.

#### Scenario: Префикс без завершающего слеша
- **WHEN** приходит `GET /api`
- **THEN** запрос уходит в wallet-web как обычный путь SPA, JWT не проверяется

#### Scenario: MCP без завершающего слеша
- **WHEN** приходит `POST /api/mcp` (без `/` в конце) с `Authorization: Bearer wmcp_…`
- **THEN** путь совпадает только с общим `/api/`, запрос проходит JWT-гейт с адресатом `wallet-api` и получает `401` гейтвея; в wallet-mcp он не уходит

### Requirement: Адреса апстримов из окружения
Гейтвей SHALL при старте читать `.env` из рабочей директории (переменные окружения
процесса имеют приоритет) и требовать `AUTH_SERVICE_URL`, `WALLET_API_URL`,
`WALLET_REPORT_API_URL`, `WALLET_AI_URL`, `WALLET_MCP_URL` и `WALLET_WEB_URL`.
Завершающие `/` в адресах отбрасываются. Если любая из них пуста, гейтвей пишет в
журнал `<KEY> must be set — <подсказка> (см. README.md)` и завершается с кодом `1`.

#### Scenario: Нет адреса wallet-ai
- **WHEN** гейтвей запускается без `WALLET_AI_URL`
- **THEN** в журнале `WALLET_AI_URL must be set — адрес wallet-ai, например http://127.0.0.1:8087 (см. README.md)`, процесс завершается с кодом 1

#### Scenario: Нет адреса wallet-mcp
- **WHEN** гейтвей запускается без `WALLET_MCP_URL`
- **THEN** в журнале `WALLET_MCP_URL must be set — адрес wallet-mcp, например http://127.0.0.1:8088 (см. README.md)`, процесс завершается с кодом 1
