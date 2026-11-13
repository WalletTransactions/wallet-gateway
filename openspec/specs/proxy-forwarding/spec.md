# proxy-forwarding Specification

## Purpose

Что гейтвей передаёт апстриму и что возвращает клиенту: путь с query, тело,
заголовки и то, что он сам добавляет или убирает (сжатие, исходный хост,
сквозной id запроса).

## Requirements

### Requirement: Запрос апстриму
Гейтвей SHALL передавать апстриму метод, исходный путь вместе с query string
(`req.target`) без пересборки, тело запроса и все заголовки, кроме `Host`,
`Content-Length`, `Connection`, `Transfer-Encoding` и `Accept-Encoding` (без учёта
регистра). Тело читается целиком до проксирования.

#### Scenario: Query string
- **WHEN** приходит `GET /api/ai/jobs/{id}?wait=25`
- **THEN** wallet-ai получает тот же путь с `?wait=25`

#### Scenario: Тело POST
- **WHEN** приходит `POST /sync/push` с JSON-телом
- **THEN** wallet-api получает то же тело

### Requirement: Без сжатия на внутреннем хопе
Гейтвей SHALL отправлять апстриму `Accept-Encoding: identity` независимо от того,
что прислал клиент, чтобы апстрим не сжимал ответ.

#### Scenario: Браузер просит gzip
- **WHEN** браузер присылает `Accept-Encoding: gzip, br` на `GET /api/reports/accounts`
- **THEN** wallet-report получает `Accept-Encoding: identity` и отвечает несжатым телом

### Requirement: Исходный хост
Если у запроса есть заголовок `Host`, гейтвей SHALL передавать его значение
апстриму в `X-Forwarded-Host`; сам `Host` апстрим получает свой.

#### Scenario: Вход по LAN-адресу
- **WHEN** браузер открывает `http://192.168.1.105:8090/auth/google`
- **THEN** google-auth получает `X-Forwarded-Host: 192.168.1.105:8090` и выбирает по нему `redirect_uri`

### Requirement: Сквозной id запроса
Гейтвей SHALL брать id запроса из входящего `X-Trace-Id` или, если его нет,
порождать новый; этот id попадает во все строки журнала запроса, передаётся
апстриму в `X-Trace-Id` и возвращается клиенту в заголовке ответа `X-Trace-Id` —
на любом ответе, включая `401` гейта и собственные пути гейтвея.

#### Scenario: Id от клиента
- **WHEN** клиент присылает `X-Trace-Id: abc123`
- **THEN** апстрим получает `X-Trace-Id: abc123`, и в ответе клиенту есть `X-Trace-Id: abc123`

#### Scenario: Id порождён гейтвеем
- **WHEN** запрос пришёл без `X-Trace-Id`
- **THEN** в ответе есть `X-Trace-Id` с новым id, и тот же id стоит в журнале гейтвея и апстрима

### Requirement: Ответ клиенту
Гейтвей SHALL возвращать клиенту статус, тело и заголовки ответа апстрима без
изменений, кроме `Host`, `Content-Length`, `Connection` и `Transfer-Encoding`;
повторяющиеся заголовки (несколько `Set-Cookie`) сохраняются все.

#### Scenario: Установка cookie при входе
- **WHEN** google-auth отвечает на `/auth/google/callback` двумя `Set-Cookie` (`session` и `refresh_token`)
- **THEN** браузер получает оба

### Requirement: Ответ буферизуется целиком
Гейтвей SHALL отдавать ответ клиенту только после того, как получил ответ
апстрима полностью. Потоковой отдачи (SSE, chunked по мере генерации) нет:
долгие операции клиенты ждут long-poll апстрима или синхронный ответ в пределах
таймаута чтения маршрута — 30 с, у `/api/docs/*` свой (см. «Недоступный апстрим» в
`request-routing`).

#### Scenario: Long-poll задания ИИ
- **WHEN** приходит `GET /api/ai/jobs/{id}?wait=25`, и wallet-ai держит запрос до готовности задания
- **THEN** клиент получает ответ одним куском, когда wallet-ai его закончил, а не частями по ходу

### Requirement: Сжатие для клиента
Гейтвей SHALL сам сжимать ответ, если клиент принимает сжатие, а тип содержимого
текстовый (решает cpp-httplib, собранный с `CPPHTTPLIB_ZLIB_SUPPORT`), и
выставлять `Content-Encoding`.

#### Scenario: Скрипт SPA
- **WHEN** браузер с `Accept-Encoding: gzip` запрашивает `GET /app.js`
- **THEN** ответ приходит с `Content-Encoding: gzip`
