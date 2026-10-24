# Tasks

Изменение не меняет код: задачи — только сверка спецификаций с кодом и живым
стеком. Найденное несоответствие исправляется в спецификации, если неточна она,
или записывается в «Наблюдения» `design.md`, если расходится документ или код
ведёт себя неожиданно. Код в рамках этого изменения не правится.

Проверка — `curl` через гейтвей (`http://localhost:8090`). Действительный токен —
из cookie `session` браузерной сессии или `Authorization: Bearer`; токены с
другим `aud`/без `aud` — подписать нельзя, поэтому такие сценарии проверяются
токеном бота (`aud` без `google-auth`) или отмечаются как проверенные по коду.
Сценарии с остановленным апстримом, занятым портом и отсутствующей переменной —
на отдельном контейнере гейтвея (`docker run` образа `servers-wallet-gateway` с
другим портом и своим окружением), рабочий контейнер не трогается.

## 1. Исходное состояние

- [x] 1.1 Сверка идёт на коммите из `design.md` (`5705ee4`) или новый коммит записан в Context; проверка — `git log -1` совпадает (HEAD `6f16a47` добавил только `openspec/` и `.claude/`; `git diff 5705ee4 HEAD -- src CMakeLists.txt Containerfile openapi.yaml README.md` пуст)
- [x] 1.2 Стек поднят, гейтвей отвечает; проверка — `curl -f http://localhost:8090/healthz` даёт `{"status":"ok"}`

## 2. Сверка по capability

Для каждой спецификации: каждый сценарий сопоставить с местом в коде и прогнать
`curl`; коды, заголовки и тексты ответов гейтвея совпадают дословно.

- [x] 2.1 `request-routing` — `find_route`, `register_routes`, `main()`; проверка — запросы на каждый префикс приходят в свой апстрим (по `request_finished` в журнале апстрима с тем же `X-Trace-Id`), `/api` без слеша и `POST /healthz` уходят в wallet-web, `HEAD /` и `TRACE /`, `502` и отсутствующая переменная — на отдельном контейнере (2026-10-23: шесть префиксов — по `X-Trace-Id` в журнале апстрима; `/`, `/join/…`, `/api` — nginx; `POST /healthz` — 405 nginx; `HEAD` 200 без тела, `TRACE` 400, `OPTIONS` проксируется; на `wallet-gateway:verify`: `502` на несуществующий хост, 25 с — 200, 35 с — 502 на 30-й секунде, без `WALLET_AI_URL` — код 1 с дословной строкой, завершающий `/` отбрасывается)
- [x] 2.2 `edge-jwt-gate` — `has_valid_token`, `handle_pre_routing`, `jwks::Cache`, `jwt_claims::check`; проверка — без токена, с битым токеном, с cookie и с `Bearer`, токен бота на `/sync/push` и на `/api/reports/*`, текст `401`, рост `jwt_claims_total`, строка `jwt_rejected` в журнале (токен бота через `/internal/telegram/token`: cookie и Bearer, приоритет Bearer, `Basic` + cookie — cookie, `x.y.z` — 401, текст 401 дословно, без `WWW-Authenticate`, бот на `/sync/push` проходит гейт, на `/api/currency-pairs` — 403 `insufficient_scope` от wallet-api, `jwt_claims_total{result="ok"}` растёт, `user_id` = `sub`. Только по коду: чужой `aud` и токен без `aud` (у бота все три `aud` гейтвея, другой токен не подписать), ротация `kid`)
- [x] 2.3 `proxy-forwarding` — `proxy_request`, `ensure_trace_id`; проверка — query и тело доходят до апстрима, апстрим получает `Accept-Encoding: identity` и `X-Forwarded-Host` (журнал или отладочный апстрим), `X-Trace-Id` в ответе, два `Set-Cookie` после входа, `Content-Encoding: gzip` на `/app.js` (на рабочем: query, тело, `X-Trace-Id` до апстрима, gzip на `/app.js` и JSON, `X-Forwarded-Host` меняет `redirect_uri` google-auth; на `verify` с echo-апстримом: `Accept-Encoding: identity`, `X-Forwarded-Host`, `Host` апстрима, два `Set-Cookie`, буферизация — первый байт через 3 с вместе с концом ответа; Р10 уточнено)
- [x] 2.4 `api-docs` — `docs.cpp`; проверка — `/docs` со списком из пяти спек, `/openapi.yaml`, `/openapi/<name>.yaml` для всех четырёх апстримов с `servers: /`, `404` на неизвестное имя, `502` при остановленном апстриме (отдельный контейнер), `/vendor/swagger-ui/swagger-ui-bundle.js` (`/docs` — пять спек, внешних ресурсов нет; четыре `/openapi/<name>.yaml` с `servers: /`; `nope` — 404 дословно; `502` дословно на `verify` с несуществующим хостом wallet-report; бандл Swagger UI — 200)
- [x] 2.5 `gateway-ops` — `/healthz`, `/version`, `/metrics`, логгер, `Containerfile`, CI; проверка — ответы трёх ручек, отсутствие `/healthz` в журнале, обрезка тела в debug-строке, `docker build` с `--build-arg GIT_COMMIT` даёт хэш в `/version`, занятый порт — на отдельном контейнере (`/healthz`, `/version`, `/metrics` с четырьмя метриками и семью метками маршрута; `/healthz` и `/metrics` в журнале нет; обрезка и `(сжато gzip` есть; `appuser`, journald; `verify` с `GIT_COMMIT=6f16a47` — хэш в `/version` и стартовой строке; занятый порт — код 1 только при процессе без `SO_REUSEPORT`, второй гейтвей стартует и делит соединения — спека поправлена, новое Р14. CI не запускался: сборка образа локально прошла)

## 3. Итоговая проверка

- [x] 3.1 `openspec validate baseline-wallet-gateway --strict`; проверка — change валиден
- [x] 3.2 Перечитать «Наблюдения» `design.md` после сверки: новые добавлены, ошибочные убраны; проверка — у каждого Р-пункта есть файл:строка (Р10 уточнено, добавлено Р14; у всех Р — файл:строка)

## Workflow follow-up

- Заархивировать change (`/opsx:archive`): спецификации переедут в `openspec/specs/`.
- Решить, какие из Р1–Р14 оформлять отдельными changes; документационные Р1–Р8 — одним change «README и openapi по коду».
