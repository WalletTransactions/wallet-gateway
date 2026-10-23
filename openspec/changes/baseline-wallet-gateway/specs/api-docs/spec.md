# Spec Delta

## Purpose

Документация API на гейтвее: Swagger UI со спекой гейтвея и спеками апстримов,
отдача этих спек и вендоренной статики Swagger UI — без внешних запросов.

## ADDED Requirements

### Requirement: Страница /docs
`GET /docs` SHALL отдавать HTML-страницу Swagger UI (`text/html; charset=utf-8`,
заголовок `wallet-gateway — API docs`) со списком спек в верхней панели:
`wallet-gateway` (`/openapi.yaml`, выбрана по умолчанию), затем `google-auth`,
`wallet-api`, `wallet-report`, `wallet-ai` (`/openapi/<name>.yaml`). «Try it out»
включён сразу. Скрипты и стили страницы берутся только с `/vendor/swagger-ui/`.

#### Scenario: Открытие документации
- **WHEN** пользователь открывает `http://<адрес>:8090/docs`
- **THEN** показана спека гейтвея, а в списке есть ещё четыре апстрима

#### Scenario: Без интернета
- **WHEN** у машины нет доступа наружу
- **THEN** `/docs` загружается полностью: все ресурсы страницы отдаёт гейтвей

### Requirement: Спека гейтвея
`GET /openapi.yaml` SHALL отдавать файл `openapi.yaml` из рабочей директории
процесса как есть (`application/yaml; charset=utf-8`). Если файла нет — `500` с
текстом `openapi.yaml не найден рядом с исполняемым файлом — сборка не докопировала его (см. CMakeLists.txt/Containerfile)`.

#### Scenario: Файл на месте
- **WHEN** приходит `GET /openapi.yaml`
- **THEN** ответ — `200` с текстом `openapi.yaml` из образа

### Requirement: Спеки апстримов через гейтвей
`GET /openapi/<name>.yaml` для `name` из `google-auth`, `wallet-api`,
`wallet-report`, `wallet-ai` SHALL на каждый запрос забирать `GET /openapi.yaml` у
этого апстрима (соединение до 3 с, ответ до 5 с) и отдавать его с блоком `servers`
верхнего уровня, заменённым на единственный `- url: /` с описанием
`Через гейтвей — тот же адрес, что у этой страницы`. Нет блока `servers` —
спека отдаётся как есть.

#### Scenario: «Try it out» идёт через гейтвей
- **WHEN** в `/docs` выбрана спека `wallet-ai`
- **THEN** её единственный сервер — `/`, и пробные запросы уходят на тот же адрес гейтвея, без CORS

### Requirement: Ошибки спек апстримов
На неизвестное имя гейтвей SHALL отвечать `404` с текстом
`gateway: нет такого апстрима — <name>`, а если апстрим не ответил или ответил не
`200` — `502` с текстом `gateway: не удалось получить openapi.yaml у <name> (<адрес>)`.

#### Scenario: Неизвестный апстрим
- **WHEN** приходит `GET /openapi/nope.yaml`
- **THEN** ответ — `404` с текстом `gateway: нет такого апстрима — nope`

#### Scenario: Апстрим лежит
- **WHEN** wallet-report остановлен, и приходит `GET /openapi/wallet-report.yaml`
- **THEN** ответ — `502` с текстом `gateway: не удалось получить openapi.yaml у wallet-report (<адрес>)`

### Requirement: Статика Swagger UI
Гейтвей SHALL отдавать файлы каталога `vendor/swagger-ui/` рабочей директории по
префиксу `/vendor/swagger-ui/`. В образ каталог попадает из `third_party/swagger-ui/`.

#### Scenario: Скрипт Swagger UI
- **WHEN** приходит `GET /vendor/swagger-ui/swagger-ui-bundle.js`
- **THEN** ответ — `200` с файлом из образа гейтвея
