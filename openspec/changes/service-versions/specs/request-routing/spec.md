# Spec Delta

## MODIFIED Requirements

### Requirement: Собственные пути гейтвея не проксируются
Гейтвей SHALL сам отвечать на `GET /docs`, `GET /openapi.yaml`,
`GET /openapi/<name>.yaml`, `GET /healthz`, `GET /metrics`, `GET /version`,
`GET /api/versions` и `GET /vendor/swagger-ui/*`, не передавая их апстримам.
Другие методы на этих путях проксируются по общей таблице: на `/api/versions` —
в wallet-api, на остальных — в wallet-web.

#### Scenario: Своя спека вместо спеки веба
- **WHEN** приходит `GET /openapi.yaml`
- **THEN** гейтвей отдаёт свой `openapi.yaml`; спека wallet-web через гейтвей по этому пути недоступна

#### Scenario: Не-GET на служебный путь
- **WHEN** приходит `POST /healthz`
- **THEN** запрос уходит в wallet-web, клиент получает ответ nginx (`405`)

#### Scenario: Сводка версий не уходит в wallet-api
- **WHEN** приходит `GET /api/versions`
- **THEN** отвечает сам гейтвей (см. capability `service-versions`), в wallet-api запрос не передаётся
