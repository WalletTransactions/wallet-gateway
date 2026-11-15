## Why

google-auth (change `add-password-auth`) принимает пароль только по HTTPS и
считает неудачные входы по IP клиента. Оба знания он берёт из заголовков
`X-Forwarded-Proto` и `X-Forwarded-For`. Сегодня гейтвей пересылает все заголовки
клиента как есть, поэтому любой клиент может сам прислать `X-Forwarded-Proto:
https` (обойти проверку TLS) или подменить `X-Forwarded-For` (обойти лимит попыток).
Гейтвей должен сам решать, чему из этих заголовков верить.

## What Changes

- Новая переменная `TRUST_FORWARDED_HEADERS` (`true`/`false`, по умолчанию `false`):
  верить ли `X-Forwarded-Proto` и `X-Forwarded-For`, пришедшим на гейтвей.
- Гейтвей сам выставляет апстриму `X-Forwarded-Proto` (схема исходного запроса) и
  `X-Forwarded-For` (адрес клиента), перезаписывая присланные клиентом значения,
  если доверия нет.
- Остальное (маршруты, JWT-гейт, `X-Forwarded-Host`, журнал тел) не меняется.

## Capabilities

### New Capabilities
Нет.

### Modified Capabilities
- `proxy-forwarding`: добавляются требования о схеме исходного запроса, адресе
  клиента и доверии к `X-Forwarded-*`.

## Impact

- **wallet-gateway**: `src/main.cpp` (сборка заголовков апстриму, разбор
  `TRUST_FORWARDED_HEADERS` при старте), `README.md` («Параметры запуска»),
  `openapi.yaml` не меняется (контракты путей те же).
- **google-auth** (`add-password-auth`): читает оба заголовка; без этого change
  проверка схемы и лимиты обходятся подделкой заголовка.
- **wallet-infra** (`add-tls-ingress`): включает `TRUST_FORWARDED_HEADERS=true` только
  вместе с TLS-прокси перед гейтвеем.
- Выкатывается до google-auth, потому что без него пароль нельзя включать безопасно.
