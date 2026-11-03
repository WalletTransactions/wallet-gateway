# Tasks

## 1. wallet-common: время запуска и `build` в метриках

- [x] 1.1 `util/metrics.{hpp,cpp}`: `set_build_info(service, version, build, commit)` запоминает время вызова; `started_at_iso()` (`YYYY-MM-DDTHH:MM:SSZ`); `render()` — `build_info{…,build="…"}` и `process_start_time_seconds`. Проверка: `wallet-telegram/tests/test_metrics.cpp` дополнен проверками этих строк и проходит после vendor.sh в wallet-telegram — проверено 2026-11-03: тест metrics_build_info_and_start_time в wallet-telegram проходит
- [x] 1.2 README wallet-common (раздел `metrics`) — новая сигнатура и `process_start_time_seconds`. Проверка: `bash wallet-common/scripts/vendor.sh --check` после раскладки по всем сервисам — без расхождений — проверено 2026-11-03: vendor.sh --check — ok во всех 7 сервисах

## 2. wallet-gateway: свой `/version`

- [x] 2.1 `CMakeLists.txt` (`APP_BUILD`, по умолчанию `${APP_VERSION}-dev`), `src/version.hpp.in` (`kBuild`), `Containerfile` (`ARG APP_BUILD`, `-DAPP_BUILD`), `main.cpp` (новая `set_build_info`, `build` в строке журнала при старте). Проверка: локальная сборка образа без аргументов → `curl :8090/version` содержит `"build":"<VERSION>-dev"` и `started_at` — проверено 2026-11-03: локальный образ без аргументов: build 0.1.1-dev, started_at; строка старта `v0.1.1-dev`
- [x] 2.2 `src/docs.cpp`: `build` и `started_at` в `/version`. Проверка: два `curl /version` подряд дают одинаковый `started_at`; `curl /metrics | grep -E 'build_info|process_start_time_seconds'` совпадает с `/version` — проверено 2026-11-03: started_at стабилен между запросами, process_start_time_seconds = started_at
- [x] 2.3 `.github/workflows/release.yml`: в rc-image `build-args` добавлен `APP_BUILD=${{ needs.meta.outputs.version }}-rc.${{ needs.meta.outputs.rc }}`. Проверка: после rc-тега `/version` гейтвея на test отдаёт `build` = тег, а smoke `deploy.sh` прошёл — проверено 2026-11-03: v0.1.1-rc.2 на test: build 0.1.1-rc.2, smoke прошёл

## 3. wallet-gateway: `GET /api/versions`

- [x] 3.1 `has_valid_token` отдаёт проверенный payload; обработчик `/api/versions`: 401 через JWT-гейт с `aud` `wallet-api`, 403 `gateway: сводка версий доступна только роли admin` при `role` ≠ `admin`, метка маршрута `GET /api/versions`. Проверка: `curl` без cookie → 401 с текстом гейтвея; с токеном роли `user` → 403 с этим текстом — проверено 2026-11-03: без токена 401 (текст гейта), роль user (Bearer и cookie, поддельный JWKS) 403 с текстом спеки, admin 200; метка GET /api/versions
- [x] 3.2 Параллельный опрос `/version` апстримов (`std::async`, таймауты 1 с + 1 с), фиксированный порядок, элементы `{"service","error":"timeout|unreachable|bad_response"}`, `Cache-Control: no-store`, `checked_at`. Регистрация до catch-all. Проверка: админским cookie `curl -s :8091/api/versions | jq` → 8 элементов; при `docker stop wallet-test-wallet-ai-1` — `unreachable` у wallet-ai, ответ 200 меньше чем за 2 с (затем `docker start`) — проверено 2026-11-03: 8 элементов под админом, unreachable/timeout (1007 мс)/bad_response на подставных адресах, no-store, POST → wallet-api
- [x] 3.3 Необязательный `WALLET_TELEGRAM_URL` в `main.cpp` (только для сводки, `find_route` не трогать). Проверка: без переменной — 7 элементов, гейтвей стартует; с ней — 8 — проверено 2026-11-03: без переменной 7 элементов, с ней 8
- [x] 3.4 `openapi.yaml` (путь `/api/versions`, 200/401/403, поля `/version` с `build`/`started_at`) и `README.md` (собственные пути, переменная `WALLET_TELEGRAM_URL`). Проверка: `GET /openapi.yaml` через гейтвей содержит `/api/versions`; `/docs` открывает его в Swagger UI — проверено 2026-11-03: /openapi.yaml через гейтвей содержит /api/versions, README — раздел «Сводка версий» и WALLET_TELEGRAM_URL

## 4. Остальные сервисы

Подробности — в change `service-versions` каждого репозитория.

- [x] 4.1 google-auth: change `service-versions` выполнен, rc выкачен в test, `/version` отдаёт `build`/`started_at`. Проверка: элемент google-auth в `/api/versions` — проверено 2026-11-03: google-auth v0.1.1-rc.3
- [x] 4.2 wallet-api — то же. Проверка: элемент wallet-api в `/api/versions` — проверено 2026-11-03: wallet-api v0.1.1-rc.3
- [x] 4.3 wallet-report — то же. Проверка: элемент wallet-report в `/api/versions` — проверено 2026-11-03: wallet-report v0.1.1-rc.2
- [x] 4.4 wallet-ai — то же. Проверка: элемент wallet-ai в `/api/versions` — проверено 2026-11-03: wallet-ai v0.1.1-rc.3
- [x] 4.5 wallet-mcp — то же. Проверка: элемент wallet-mcp в `/api/versions` — проверено 2026-11-03: wallet-mcp v0.1.0-rc.2
- [x] 4.6 wallet-telegram — то же. Проверка: элемент wallet-telegram в `/api/versions` — проверено 2026-11-03: wallet-telegram v0.1.0-rc.2

## 5. wallet-infra

- [x] 5.1 `compose.yaml`: `WALLET_TELEGRAM_URL: http://wallet-telegram:8086` в environment гейтвея; README инфры — строка о переменной. Проверка: `deploy.sh` test для wallet-infra и гейтвея проходит, `/api/versions` содержит wallet-telegram — проверено 2026-11-03: wallet-infra v0.1.0-rc.6 выкачен, WALLET_TELEGRAM_URL у гейтвея, wallet-telegram в сводке

## 6. Сквозная проверка

- [x] 6.1 На test после выката всех rc: `/api/versions` под админом — у всех восьми сервисов `build` совпадает с `~/.local/state/wallet-envs/test/tags.env`, `started_at` совпадает с `docker inspect -f '{{.State.StartedAt}}'` с точностью до секунды — проверено 2026-11-03: все 8: build = tags.env, started_at = docker StartedAt (±1 с)

## Workflow follow-up

- Тест-задача SemiAutoTest на сквозную проверку (6.1 и бейдж wallet-web) — после выката.
- Архив change во всех репозиториях после закрытия тест-задачи.
