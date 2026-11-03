# Design

## Context

- Все C++-сервисы (google-auth, wallet-api, wallet-report, wallet-ai, wallet-mcp,
  wallet-telegram, wallet-gateway) отдают `GET /version` из `src/docs.cpp`.
  Значения берутся из `version.hpp`: его генерирует CMake из `VERSION` и
  `-DGIT_COMMIT`. wallet-web отдаёт `/etc/nginx/version.json`, который собирается
  в Containerfile. Внутренний `/version` веба наружу не виден: на этот путь
  гейтвей отвечает сам.
- `metrics::set_build_info(service, version, commit)` из wallet-common вызывается
  один раз в `main()` до `listen()`. Копия wallet-common лежит в
  `<сервис>/third_party/wallet-common/` и раскладывается
  `wallet-common/scripts/vendor.sh`.
- `.github/workflows/release.yml` одинаков во всех сервисах. rc-тег собирает
  образ `X.Y.Z-rc.N` (`--build-arg GIT_COMMIT`). Финальный тег `X.Y.Z` копирует
  тот же манифест байт в байт (`skopeo --preserve-digests`), без пересборки.
- Smoke выката (`wallet-infra/infra/env/deploy.sh`, `smoke_version`) сравнивает
  `version` из `/version` с `X.Y.Z` тега, а `git_commit` — с ревизией образа.
- JWT-гейт гейтвея проверяет подпись, `exp`, `iss`, `aud`, но не роли. Токен
  google-auth содержит claim `role` (`user`/`admin`).
- Все сервисы контура в одной сети `wallet` compose. Бот (`wallet-telegram:8086`)
  через гейтвей не публикуется.

## Goals / Non-Goals

**Goals:**
- Одним запросом видеть точную сборку каждого сервиса (с rc-номером) и время её
  запуска.
- Не трогать то, на что опирается smoke выката.

**Non-Goals:**
- Состояние здоровья сервисов (это `/healthz` и Prometheus), история выкатов.
- Версии БД, Redis, LM Studio и Android-клиента.
- Кэширование сводки: её запрашивает только админ, и только по наведению.

## Decisions

### D1. Новое поле `build`, а `version` не трогаем
`version` остаётся номером из `VERSION` (`0.1.1`), `build` — тег образа
(`0.1.1-rc.2`).
*Альтернатива* — класть тег в `version`. Это сломало бы `smoke_version`: для
финального тега он ждёт `0.1.1`, а в бинарнике было бы `0.1.1-rc.2`. Кроме того,
пришлось бы переписать спеки всех сервисов, где сказано «`version` из файла
`VERSION`».

### D2. `build` вшивается при сборке (`APP_BUILD`), а не задаётся через окружение
Release передаёт `--build-arg APP_BUILD=<X.Y.Z-rc.N>`. Containerfile передаёт его
в `cmake -DAPP_BUILD=…`, CMake подставляет его в `version.hpp` (`kBuild`). Если
параметр пустой или не задан, подставляется `${APP_VERSION}-dev`, чтобы локальную
сборку нельзя было спутать с релизной.
Финальный `X.Y.Z` — тот же образ, поэтому в проде он покажет `build` =
`X.Y.Z-rc.N`. Так видно, какой именно rc промоутнут, и digest остаётся прежним.
*Альтернатива* — переменная окружения из compose (`APP_BUILD: ${WALLET_API_TAG}`).
Пересборка не нужна, но значение может разойтись с реальным образом (ручной
`docker run`, локальный compose), и сервис тогда показывает не себя.

### D3. `started_at` — время вызова `set_build_info`
В wallet-common сигнатура становится `metrics::set_build_info(service, version,
build, commit)`. Функция запоминает текущее время, а `metrics::started_at_iso()`
возвращает его как `YYYY-MM-DDTHH:MM:SSZ`. `render()` отдаёт `build_info` с
меткой `build` и `process_start_time_seconds`, имя которой принято в Prometheus.
Вызов стоит в `main()` до `listen()`, так что отставание от фактического старта
процесса — миллисекунды. `/proc/self/stat` не используем: код проще, и юнит-тест
метрик (`wallet-telegram/tests/test_metrics.cpp`) проверяет время без procfs. Каждый `docs.cpp` добавляет в `/version`
`build` и `started_at`; правок по сервисам — по три строки.

### D4. Сводка — собственный обработчик гейтвея, параллельные запросы
`GET /api/versions` регистрируется до catch-all `Get(".*", proxy)`, рядом с
`/version`. Порядок такой:
1. Токен проверяется тем же `has_valid_token(..., "wallet-api")`. Если он не
   прошёл — 401 с текстом гейтвея.
2. Из проверенного payload читается `role`. Если не `admin` — 403.
3. На каждый апстрим запускается `std::async`: `httplib::Client`,
   `set_connection_timeout(1s)`, `set_read_timeout(1s)`, `GET /version`.
4. Результаты собираются в фиксированном порядке (spec `service-versions`).

Правило «роли проверяет апстрим» здесь не нарушается: у этого пути апстрима нет,
сервис — сам гейтвей. Для этого `has_valid_token` начинает возвращать payload
(или получает выходной параметр), а не только `bool`.
*Альтернатива* — отдать сводку всем вошедшим и скрывать её в вебе. Тогда
внутреннее устройство видно любому пользователю с токеном; отклонено.
*Альтернатива* — кэшировать ответы на N секунд. Это экономия на редком запросе,
а цена — ответ, который «врёт» сразу после выката; отклонено.

### D5. Бот — через необязательный `WALLET_TELEGRAM_URL`
Адрес нужен только для опроса. `find_route` его не видит, поэтому бот не
становится публичным. В `wallet-infra/compose.yaml` у гейтвея появляется
`WALLET_TELEGRAM_URL: http://wallet-telegram:8086` (та же сеть `wallet`).

### D6. Спеки других сервисов — их собственные change
В google-auth, wallet-api, wallet-report, wallet-ai, wallet-mcp и wallet-telegram
открывается change `service-versions`: proposal, дельта требования о `/version`
и tasks, без design, со ссылкой сюда. wallet-web — отдельный change с бейджем.
У wallet-common и wallet-infra спек `/version` нет, их правки описаны в задачах
этого change.

## Risks / Trade-offs

- [Сервис ещё не пересобран, в его `/version` нет `build`/`started_at`] → сводка
  передаёт ответ как есть, веб показывает «—». Совместимость с такими сборками
  специально не поддерживаем: стек выкатывается целиком.
- [8 потоков `std::async` на запрос] → сводку вызывает только админ, а запрос
  ограничен ~2 с. Пул cpp-httplib не занят ожиданием, кроме одного обработчика.
- [Изменение сигнатуры `set_build_info` ломает сборку сервиса, пока не поправлен
  его `main.cpp`] → vendor.sh и правка `main.cpp` делаются в одном коммите
  сервиса; перед rc-тегом — `bash wallet-common/scripts/vendor.sh --check`
  (в CI его нет).
- [`ci.yml` тоже собирает образ, но без `APP_BUILD`] → такой образ показывает
  `-dev`, и это правда: он не публикуется.

## Migration Plan

1. wallet-common: `metrics` + тесты, vendor.sh по всем сервисам.
2. Каждый C++-сервис: `version.hpp.in`, CMake, Containerfile, `docs.cpp`,
   `main.cpp`, release.yml, rc-тег. Сборки — строго по одной (OOM при
   параллельной сборке).
3. wallet-gateway: `/api/versions` (можно вместе с п. 2 для гейтвея).
4. wallet-infra: `WALLET_TELEGRAM_URL` в compose, rc-тег инфры.
5. wallet-web: `version.json` + бейдж.

Откат: предыдущие rc-теги каждого сервиса. Новые поля — добавочные, и smoke их
не читает.
