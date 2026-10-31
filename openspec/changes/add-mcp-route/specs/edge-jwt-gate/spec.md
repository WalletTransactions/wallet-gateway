## MODIFIED Requirements

### Requirement: Закрытые префиксы
Гейтвей SHALL проверять access-токен на путях `/api/*` (включая `/api/reports/*` и
`/api/ai/*`) и `/sync/*`, кроме `/api/mcp/*`, и SHALL NOT проверять его на
`/api/mcp/*`, `/auth/*`, `/admin/*`, на фолбэке в wallet-web и на собственных путях
гейтвея.

#### Scenario: Анонимный запрос к API
- **WHEN** без токена приходит `GET /api/currency-pairs`
- **THEN** гейтвей отвечает `401`, wallet-api не вызывается

#### Scenario: Анонимный запрос ко входу
- **WHEN** без токена приходит `GET /auth/me`
- **THEN** запрос уходит в google-auth, и клиент получает его собственный ответ

#### Scenario: Анонимный запрос к MCP
- **WHEN** без токена приходит `POST /api/mcp/`
- **THEN** запрос уходит в wallet-mcp, и клиент получает его собственный ответ (`401` wallet-mcp, а не текст гейтвея)

## ADDED Requirements

### Requirement: MCP-префикс проверяет апстрим
На `/api/mcp/*` гейтвей SHALL передавать `Authorization` и `Cookie` апстриму как
есть, не разбирая токен: ни подписи, ни `exp`, ни `iss`/`aud`. Непрозрачный
токен `wmcp_…` проверяет только wallet-mcp. Счётчик `jwt_claims_total` на этом
префиксе не меняется, `user_id` в журнале запроса гейтвея не заполняется.

#### Scenario: Непрозрачный MCP-токен
- **WHEN** на `POST /api/mcp/` приходит `Authorization: Bearer wmcp_abc`
- **THEN** гейтвей не отвечает `401` сам, запрос с тем же заголовком доходит до wallet-mcp, `jwt_claims_total` не растёт

#### Scenario: JWT на MCP-префиксе
- **WHEN** на `POST /api/mcp/` приходит действительный пользовательский JWT
- **THEN** гейтвей его тоже не проверяет и передаёт как есть; принимать ли его, решает wallet-mcp
