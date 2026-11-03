# Spec Delta

## MODIFIED Requirements

### Requirement: Версия сборки
`GET /version` SHALL отвечать JSON с полями `service` (`wallet-gateway`),
`version` (из файла `VERSION`), `build` (тег образа, переданный на сборке через
`APP_BUILD`, например `0.1.1-rc.1`; без него — `<VERSION>-dev`), `git_commit`
(короткий хэш, переданный на сборке через `GIT_COMMIT`, иначе `unknown`) и
`started_at` (момент запуска процесса, ISO-8601 UTC с точностью до секунды,
`2026-11-03T19:24:28Z`). `service`, `version`, `build` и `git_commit` — также в
`build_info` на `/metrics` и в строке журнала при старте.

#### Scenario: Сборка без хэша
- **WHEN** образ собран без `--build-arg GIT_COMMIT` и без `--build-arg APP_BUILD`
- **THEN** `GET /version` отвечает `{"build":"<VERSION>-dev","git_commit":"unknown","service":"wallet-gateway","started_at":"<время запуска>","version":"<VERSION>"}`

#### Scenario: Сборка релиза
- **WHEN** образ собран Release-workflow по тегу `v0.1.1-rc.2`
- **THEN** `version` = `0.1.1`, `build` = `0.1.1-rc.2`, и после промоута в `0.1.1` тот же образ отдаёт те же значения

#### Scenario: Время запуска не меняется между запросами
- **WHEN** `GET /version` вызывается дважды без перезапуска процесса
- **THEN** `started_at` в обоих ответах одинаковый, а после перезапуска контейнера — новый

### Requirement: Метрики
`GET /metrics` SHALL отдавать метрики в текстовом формате Prometheus
(`text/plain; version=0.0.4; charset=utf-8`): `build_info`,
`process_start_time_seconds` (момент запуска процесса, секунды Unix-эпохи),
`http_requests_total` и `http_request_duration_seconds` по методу, грубому
маршруту и статусу, а также `jwt_claims_total`. Метка маршрута — грубая категория гейтвея: `proxy /api/reports/*`,
`proxy /api/ai/*`, `proxy /api/*`, `proxy /sync/*`, `proxy /auth/*`,
`proxy /admin/*`, `proxy (static)`, `GET /api/versions`.

#### Scenario: Запросы к API
- **WHEN** через гейтвей прошёл `GET /api/currency-pairs` с ответом `200`
- **THEN** `http_requests_total{method="GET",route="proxy /api/*",status="200"}` вырос на 1

#### Scenario: Запрос сводки версий
- **WHEN** администратор получил ответ `200` на `GET /api/versions`
- **THEN** `http_requests_total{method="GET",route="GET /api/versions",status="200"}` вырос на 1

#### Scenario: Время запуска в метриках
- **WHEN** запрашивается `GET /metrics`
- **THEN** в ответе есть `process_start_time_seconds`, и он совпадает с `started_at` из `/version` с точностью до секунды
