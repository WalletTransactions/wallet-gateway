# Tasks

## 1. Разбор настройки

- [ ] 1.1 Читать `TRUST_FORWARDED_HEADERS` при старте (`true`/`false`, по умолчанию `false`, иначе ошибка в журнале и код `1`); проверить запуском с `TRUST_FORWARDED_HEADERS=yes` (ошибка, код `1`) и без переменной (старт)

## 2. Заголовки апстриму

- [ ] 2.1 Не копировать присланные `X-Forwarded-Proto`/`X-Forwarded-For` и выставлять свои по правилам спеки; проверить `curl` на живом стеке без доверия: запрос с `X-Forwarded-Proto: https` и `X-Forwarded-For: 10.9.9.9` доходит до google-auth как `http` и с адресом соединения (смотреть журнал google-auth или временный эхо-апстрим)
- [ ] 2.2 Режим доверия: передавать `https`/`http` и присланный `X-Forwarded-For`, мусорное значение схемы заменять на `http`; проверить `curl` при `TRUST_FORWARDED_HEADERS=true` по трём сценариям спеки

## 3. Документация

- [ ] 3.1 Описать переменную в `README.md` («Параметры запуска») и проверить, что `openapi.yaml` не требует правок (пути и ответы те же); `openspec validate trusted-forwarded-headers --strict` проходит

## Workflow follow-up

- Выкатить до включения паролей в google-auth; флаг `true` включает wallet-infra вместе с TLS-профилем.
- Архивировать change после выкатки и проверки на test-контуре.
