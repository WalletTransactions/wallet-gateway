# Spec Delta

## Purpose

Грубая проверка access-токена на входе в закрытые префиксы: откуда гейтвей берёт
токен, как проверяет подпись, срок и адресата и как отказывает. Это фильтр
«токен настоящий и выпущен для этого сервиса», а не авторизация: роли и scope
проверяет апстрим.

## ADDED Requirements

### Requirement: Закрытые префиксы
Гейтвей SHALL проверять access-токен на путях `/api/*` (включая `/api/reports/*` и
`/api/ai/*`) и `/sync/*` и SHALL NOT проверять его на `/auth/*`, `/admin/*`, на
фолбэке в wallet-web и на собственных путях гейтвея.

#### Scenario: Анонимный запрос к API
- **WHEN** без токена приходит `GET /api/currency-pairs`
- **THEN** гейтвей отвечает `401`, wallet-api не вызывается

#### Scenario: Анонимный запрос ко входу
- **WHEN** без токена приходит `GET /auth/me`
- **THEN** запрос уходит в google-auth, и клиент получает его собственный ответ

### Requirement: Источник токена
Гейтвей SHALL брать токен из заголовка `Authorization: Bearer <токен>`, а если
заголовка нет или он не начинается с `Bearer ` — из cookie `session`.

#### Scenario: Браузер
- **WHEN** браузер присылает только cookie `session`
- **THEN** проверяется токен из cookie

#### Scenario: Заголовок и cookie вместе
- **WHEN** запрос несёт и `Authorization: Bearer`, и cookie `session`
- **THEN** проверяется токен из заголовка

### Requirement: Подпись и срок токена
Токен SHALL проходить гейт, только если в заголовке JWT есть `kid`, ключ с этим
`kid` есть в JWKS google-auth (`GET <AUTH_SERVICE_URL>/.well-known/jwks.json`),
подпись RS256 верна и `exp` не истёк. JWKS загружается при старте (ошибка не
фатальна) и перезапрашивается один раз, когда встречается незнакомый `kid`.

#### Scenario: Ротация ключа
- **WHEN** google-auth сменил ключ, и приходит токен с новым `kid`
- **THEN** гейтвей перечитывает JWKS и проверяет токен новым ключом

#### Scenario: Битый токен
- **WHEN** приходит `Authorization: Bearer x.y.z`
- **THEN** гейтвей отвечает `401`

### Requirement: Адресат токена
Токен SHALL проходить гейт, только если `iss` — `google-auth`, а `aud` содержит
адресата маршрута (`wallet-report`, `wallet-ai` или `wallet-api`, см. «Маршруты
API» в `request-routing`). Токен без `aud` (старого формата) не проходит. `scope`,
роль и `device_id` гейтвей не проверяет.

#### Scenario: Токен другого сервиса
- **WHEN** на `POST /sync/push` приходит токен с `aud: ["wallet-report"]`
- **THEN** гейтвей отвечает `401`

#### Scenario: Токен бота
- **WHEN** на `POST /sync/push` приходит токен с `aud` `wallet-api` и `scope: "reports:read expense:write"` без `wallet:write`
- **THEN** гейтвей пропускает запрос; что боту можно, решает wallet-api

### Requirement: Отказ гейта
При непройденной проверке гейтвей SHALL ответить сам, не вызывая апстрим и не
читая тело запроса: статус `401`, `Content-Type: text/plain; charset=utf-8`, тело
`gateway: access-токен отсутствует, просрочен или подпись не совпала — войдите через /auth/google (или обновите его через /auth/refresh) и повторите запрос`.
Заголовка `WWW-Authenticate` в ответе нет.

#### Scenario: Просроченный токен из браузера
- **WHEN** cookie `session` несёт токен с истёкшим `exp`
- **THEN** ответ — `401` с этим текстом, и клиент (веб) идёт в `POST /auth/refresh`

### Requirement: Токен уходит апстриму как есть
Прошедший гейт запрос SHALL уходить апстриму с исходными `Authorization` и
`Cookie`. Гейтвей SHALL NOT добавлять заголовков с личностью пользователя
(`X-User-*` и подобных): апстрим сам перепроверяет токен.

#### Scenario: Апстрим перепроверяет
- **WHEN** токен прошёл гейт, но у пользователя нет нужного scope
- **THEN** отказ (`403`) приходит от апстрима, а не от гейтвея

### Requirement: Журнал и метрики гейта
Для токена с верной подписью гейтвей SHALL увеличивать счётчик
`jwt_claims_total{result=...}` (`ok`, `legacy`, `wrong_issuer`, `wrong_audience`).
При отказе по `iss`/`aud` в журнал пишется предупреждение `access token rejected`
с полями `event: jwt_rejected`, `reason` и `audience`. Для прошедшего токена
`sub` становится `user_id` всех строк журнала этого запроса.

#### Scenario: Старый токен
- **WHEN** приходит токен с верной подписью, но без `aud`
- **THEN** ответ — `401`, `jwt_claims_total{result="legacy"}` растёт, в журнале `jwt_rejected` с `reason: legacy`
