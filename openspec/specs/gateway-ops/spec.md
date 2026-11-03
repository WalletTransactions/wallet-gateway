# gateway-ops Specification

## Purpose

Эксплуатация гейтвея: проверка живости, версия сборки, метрики Prometheus,
журнал запросов, параметры запуска, образ и CI.

## Requirements

### Requirement: Живость гейтвея
`GET /healthz` SHALL отвечать `200` с телом `{"status":"ok"}`
(`application/json; charset=utf-8`), не обращаясь к апстримам: проверяется только
сам гейтвей. Healthcheck compose — `curl -f http://127.0.0.1:8090/healthz`.

#### Scenario: Апстрим лежит
- **WHEN** wallet-api остановлен
- **THEN** `GET /healthz` всё равно отвечает `200`, и compose не перезапускает гейтвей

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

### Requirement: Строка журнала на запрос
Гейтвей SHALL писать для каждого запроса, кроме `/healthz` и `/metrics`, строку
уровня info `<МЕТОД> <путь> -> <статус>` с полями `event: request_finished` и
`duration_ms`.

#### Scenario: Проба compose
- **WHEN** compose каждые 10 с вызывает `GET /healthz`
- **THEN** в журнале гейтвея этих запросов нет

### Requirement: Тела в отладочном журнале
Для тех же запросов гейтвей SHALL писать строку уровня debug с
`event: request_body`, телом запроса и телом ответа. Тела длиннее 1024 байт
обрезаются с пометкой `...(обрезано, всего <N> байт)`; сжатое тело ответа не
пишется — вместо него `(сжато <кодировка>, <N> байт)`.

#### Scenario: Большой ответ
- **WHEN** проксируется ответ на 50 000 байт без сжатия
- **THEN** в debug-строке тело обрезано до 1024 байт с пометкой `...(обрезано, всего 50000 байт)`

### Requirement: Куда пишется журнал
Журнал SHALL писаться в stdout, а если задан `LOG_DIR` — в файл на каждый день
в этом каталоге; `LOG_DIR=stdout` или `-` — снова stdout. На Windows без
`LOG_DIR` журнал пишется в каталог `logs`.

#### Scenario: Контейнер
- **WHEN** гейтвей запущен в compose без `LOG_DIR`
- **THEN** журнал идёт в stdout контейнера (драйвер journald)

### Requirement: Параметры запуска
Гейтвей SHALL слушать `HOST:PORT` из окружения (по умолчанию `127.0.0.1:8090`; в
compose `0.0.0.0:8090`, порт `8090` проброшен наружу). При старте в журнал пишется
`wallet-gateway v<версия> (<хэш>) listening on http://<HOST>:<PORT> (...)`.

#### Scenario: Запуск в compose
- **WHEN** compose запускает гейтвей с `HOST=0.0.0.0` и `PORT=8090`
- **THEN** в журнале `wallet-gateway v<версия> (<хэш>) listening on http://0.0.0.0:8090 (...)`

### Requirement: Занятый порт
Если порт занять не удалось, гейтвей SHALL писать ошибку
`не удалось запустить сервер на <HOST>:<PORT> (порт занят или недоступен?)` и
завершаться с кодом `1`. Сокет открывается с `SO_REUSEPORT` (Linux, умолчание
cpp-httplib), поэтому порт, который держит процесс того же пользователя тоже с
`SO_REUSEPORT`, занятым не считается.

#### Scenario: Порт занят другим процессом
- **WHEN** порт 8090 держит процесс без `SO_REUSEPORT`
- **THEN** гейтвей пишет эту ошибку и завершается с кодом 1

#### Scenario: Второй экземпляр гейтвея
- **WHEN** на том же хосте и порту под тем же пользователем запускается второй гейтвей
- **THEN** он стартует, и ядро распределяет входящие соединения между обоими процессами

### Requirement: Образ и CI
Образ SHALL собираться двухэтапным `Containerfile` на `debian:bookworm-slim`
(`BUILD_JOBS`, по умолчанию 3) и запускаться от пользователя `appuser` с
`openapi.yaml` и `vendor/swagger-ui/` рядом с бинарником. CI (GitHub Actions) на
push в `main` и на pull request, кроме изменений только в `*.md`, SHALL
собирать образ с `GIT_COMMIT` из коммита и никуда его не публиковать.

#### Scenario: Pull request
- **WHEN** открыт pull request с правкой `src/main.cpp`
- **THEN** CI собирает образ; при ошибке компиляции проверка падает
