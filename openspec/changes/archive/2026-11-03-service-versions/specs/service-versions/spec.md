# Spec Delta

## Purpose

Сводка версий сервисов контура одним запросом через гейтвей: какие сборки
выкачены и когда запущены. Нужна владельцу (бейдж версии в вебе) и
автоматизации тест-задач.

## ADDED Requirements

### Requirement: Сводка версий
`GET /api/versions` SHALL отвечать `200` с JSON `{"checked_at": <ISO-8601 UTC>,
"services": [...]}` и заголовком `Cache-Control: no-store`. В `services` первым
идёт ответ `/version` самого гейтвея, за ним в этом порядке — ответы `GET /version`
апстримов: google-auth, wallet-api, wallet-report, wallet-ai, wallet-mcp,
wallet-telegram, wallet-web. Ответ апстрима кладётся как есть, без переименования
полей.

#### Scenario: Все сервисы отвечают
- **WHEN** администратор запрашивает `GET /api/versions`, и все апстримы живы
- **THEN** ответ `200`, в `services` восемь объектов (`wallet-gateway` первым, `wallet-web` последним), у каждого есть `service`, `version`, `build`, `git_commit`, `started_at`

#### Scenario: Ответ не кэшируется
- **WHEN** приходит ответ на `GET /api/versions`
- **THEN** в нём есть заголовок `Cache-Control: no-store`

### Requirement: Доступ только администратору
`GET /api/versions` SHALL требовать access-токен (Bearer или cookie `session`),
проверенный так же, как на `/api/*`, с адресатом `aud` = `wallet-api`. Без
валидного токена ответ — `401` с тем же текстом, что у JWT-гейта. С валидным
токеном, у которого claim `role` не равен `admin`, ответ — `403` с телом
`gateway: сводка версий доступна только роли admin`. Апстримы при этом не
опрашиваются.

#### Scenario: Без токена
- **WHEN** приходит `GET /api/versions` без Bearer и без cookie `session`
- **THEN** ответ `401`, тело — текст JWT-гейта, ни один апстрим не опрошен

#### Scenario: Обычный пользователь
- **WHEN** приходит `GET /api/versions` с валидным токеном роли `user`
- **THEN** ответ `403` с телом `gateway: сводка версий доступна только роли admin`

### Requirement: Недоступный сервис не ломает сводку
Гейтвей SHALL опрашивать апстримы параллельно, ограничивая каждый запрос двумя
секундами: одна на соединение, одна на чтение. Если апстрим не ответил, ответил
не `200` или не JSON-объектом, его элемент в `services` SHALL быть
`{"service": "<имя>", "error": "<причина>"}`. Причина: `timeout`, `unreachable`
или `bad_response`. Ответ на `GET /api/versions` при этом остаётся `200`.

#### Scenario: Один сервис лежит
- **WHEN** wallet-ai остановлен, а остальные живы
- **THEN** ответ `200`, элемент wallet-ai — `{"service":"wallet-ai","error":"unreachable"}`, остальные элементы — их `/version`

#### Scenario: Сервис завис
- **WHEN** апстрим принимает соединение, но не отвечает
- **THEN** не позже чем через ~2 с после запроса приходит ответ `200`, а у этого апстрима `"error":"timeout"`

### Requirement: Бот — по необязательному адресу
wallet-telegram SHALL попадать в сводку, только если задана переменная
`WALLET_TELEGRAM_URL`. Она используется только для опроса `/version`, бот через
гейтвей не проксируется. Если переменная не задана, элемента wallet-telegram в
`services` нет, и гейтвей стартует как раньше.

#### Scenario: Адрес бота не задан
- **WHEN** гейтвей запущен без `WALLET_TELEGRAM_URL`
- **THEN** в `services` семь элементов, без wallet-telegram

#### Scenario: Бот не проксируется
- **WHEN** задан `WALLET_TELEGRAM_URL`, и приходит `GET /healthz-telegram` или любой другой путь
- **THEN** маршрутизация не меняется: ни один путь, кроме опроса сводки, в бота не уходит
