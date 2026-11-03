# Proposal

## Why

Узнать, какие версии сервисов сейчас крутятся на контуре, можно только с хоста:
`tags.env`, `docker ps` или `curl` к `/version` изнутри сети контура. Снаружи
гейтвей отвечает на `/version` только за себя. Кроме того, `version` в `/version`
берётся из файла `VERSION`, поэтому все rc одной версии (`0.1.1-rc.1`, `-rc.2`, …)
выглядят одинаково (`0.1.1`), и различить их можно только по `git_commit`.
Владельцу и автоматизации тест-задач нужно видеть выкаченные сборки и время их
запуска прямо из веба.

## What Changes

- `GET /version` всех сервисов стека получает два поля: `build` — тег образа, из
  которого собран бинарник (`0.1.1-rc.1`; при локальной сборке — `<VERSION>-dev`),
  и `started_at` — момент запуска процесса (ISO-8601 UTC). Поля `service`,
  `version`, `git_commit` не меняются: по `version` сверяет выкат smoke в
  `wallet-infra/infra/env/deploy.sh`.
- Release-workflow каждого сервиса передаёт тег в сборку (`--build-arg APP_BUILD`).
- `/metrics` всех C++-сервисов дополнительно отдаёт `process_start_time_seconds`.
- Новый собственный путь гейтвея `GET /api/versions`: гейтвей параллельно
  опрашивает `/version` всех апстримов (google-auth, wallet-api, wallet-report,
  wallet-ai, wallet-mcp, wallet-web и, если задан адрес, wallet-telegram) и
  отдаёт сводный список вместе со своей версией. Доступен только с валидным
  access-токеном с ролью `admin`.
- Гейтвей получает необязательную переменную `WALLET_TELEGRAM_URL`, только для
  опроса версии бота. Бот через гейтвей по-прежнему не публикуется.

Бейдж версии в вебе делается отдельным change `service-versions` в wallet-web.
Поля `/version` в остальных сервисах описывают одноимённые change в их
репозиториях.

## Capabilities

### New Capabilities
- `service-versions`: сводка версий сервисов контура `GET /api/versions`: состав
  опрашиваемых сервисов, доступ, таймауты, ответ при недоступном сервисе.

### Modified Capabilities
- `gateway-ops`: «Версия сборки» — в `/version` добавляются `build` и `started_at`,
  в `/metrics` — `process_start_time_seconds`.
- `request-routing`: в «Собственные пути гейтвея не проксируются» добавляется
  `GET /api/versions`. Без этого путь ушёл бы по общему префиксу `/api/` в
  wallet-api.

## Impact

- **wallet-gateway**: `src/docs.cpp` (`/version`), новый обработчик
  `/api/versions`, `src/main.cpp` (регистрация до catch-all, необязательная
  `WALLET_TELEGRAM_URL`), `CMakeLists.txt` и `src/version.hpp.in` (`APP_BUILD`),
  `Containerfile`, `.github/workflows/release.yml`, `openapi.yaml`, `README.md`.
- **wallet-common**: `util/metrics.*` — время запуска процесса
  (`started_at`, `process_start_time_seconds`); раскопировать по сервисам через
  `scripts/vendor.sh`.
- **google-auth, wallet-api, wallet-report, wallet-ai, wallet-mcp,
  wallet-telegram**: `/version` с `build`/`started_at`, `APP_BUILD` в CMake,
  Containerfile и release.yml. У каждого — свой change `service-versions` с
  дельтой спеки.
- **wallet-web**: `version.json` с `build`/`started_at`, бейдж в интерфейсе
  (свой change).
- **wallet-infra**: `compose.yaml` — `WALLET_TELEGRAM_URL` для гейтвея.
- Smoke выката не меняется: он читает только `version` и `git_commit`.
