# Proposal

## Why

Клиентам (Android и web) нужен путь к новому сервису `wallet-docs`, который отвечает на
вопросы о системе по OpenSpec-спекам (см. change `wallet-docs-assistant` в wallet-infra).
Сервис необязательный: гейтвей должен работать без него и сообщать об этом предсказуемо,
чтобы клиент мог скрыть раздел.

## What Changes

- Новый префикс `/api/docs/` → `wallet-docs` (`WALLET_DOCS_URL`), JWT-гейт с адресатом
  `aud` = `wallet-docs`. Префикс стоит раньше общего `/api/`.
- `WALLET_DOCS_URL` **необязательна**: в отличие от остальных апстримов, её отсутствие
  не мешает старту. Тогда любой запрос на `/api/docs/*` получает `404` с телом
  `gateway: docs не подключён` без проверки токена и без обращения к апстриму.
- Если `WALLET_DOCS_URL` задан, а сервис недоступен — обычное поведение гейтвея: `502`.
- Заливка индекса (`PUT /docs/index/{repo}`) через гейтвей **не** проходит: путь `/docs`
  — собственный Swagger-путь гейтвея, а индексацию делает Jenkins напрямую по сети контура.
- `openapi.yaml` гейтвея описывает новый префикс одной строкой в таблице маршрутов;
  контракт ручек — в `openapi.yaml` самого `wallet-docs`.

## Capabilities

### New Capabilities

### Modified Capabilities
- `request-routing`: новая строка в таблице маршрутов, порядок префиксов, необязательный
  адрес апстрима `WALLET_DOCS_URL` и ответ `404`, если он не задан.

## Impact

- **wallet-gateway**: таблица маршрутов, чтение окружения, `openapi.yaml`, README, тесты маршрутизации.
- **google-auth**: токены должны нести `wallet-docs` в `aud` (change `docs-audience`).
- **wallet-infra**: `WALLET_DOCS_URL` в `compose.yaml` без `depends_on` (change `wallet-docs-assistant`).
- Остальные маршруты и `edge-jwt-gate` не меняются: гейт уже действует на `/api/*`.
