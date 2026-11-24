## Why

Аудит безопасности 2026-11-23: гейтвей принимал тело до 100 МБ (дефолт
httplib), буферизовал его целиком и копировал в запрос к апстриму, а контейнер
ограничен `mem_limit: 64m`. Маршруты без JWT-гейта (`/auth/*`, `/admin/*`,
статика) читают тело и без токена — несколько параллельных больших `POST`
убивали гейтвей по OOM, а с ним весь вход в стек.

## What Changes

- Предел тела запроса — 10 МБ на всех маршрутах; больше — `413` без чтения
  тела и без вызова апстрима.

## Capabilities

### Modified Capabilities

- `proxy-forwarding` — предел тела запроса.

## Impact

- Код: `src/main.cpp` (`set_payload_max_length`); `openapi.yaml` (`413`),
  `README.md`.
- Самое большое легитимное тело — выписка в wallet-ai (его предел 8 МБ),
  проходит. В wallet-api свой предел 8 МБ (change `reader-write-guard`).
- Отдельно стоит задать `request_body max_size` в Caddyfile, который пишет
  `wallet-infra/infra/env/tls.sh`, — не входит в этот change.
