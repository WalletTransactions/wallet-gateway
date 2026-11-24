# Tasks

## 1. Предел тела

- [x] 1.1 `svr.set_payload_max_length(10 МБ)` в `main.cpp`; проверить сборку в Docker (`-j3`)
- [x] 1.2 `openapi.yaml` (`413` у проксируемых маршрутов) и `README.md`; проверить, что `GET /openapi.yaml` разбирается
- [ ] 1.3 На test: `curl -X POST --data-binary @11MB …/auth/login` → `413`; импорт выписки 5 МБ в вебе проходит

## 2. Спеки

- [x] 2.1 `openspec validate request-body-limit --strict`
- [ ] 2.2 Архивировать change после проверки на test
