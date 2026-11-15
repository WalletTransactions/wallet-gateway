# Spec Delta

## ADDED Requirements

### Requirement: Доверие к заголовкам прокси
Переменная `TRUST_FORWARDED_HEADERS` SHALL принимать `true` или `false` (по
умолчанию `false`); другое значение SHALL останавливать запуск с ошибкой в
журнале и кодом `1`. Значение `true` включают только если перед гейтвеем стоит
TLS-прокси, который сам перезаписывает `X-Forwarded-Proto` и `X-Forwarded-For`.

#### Scenario: Неверное значение
- **WHEN** гейтвей стартует с `TRUST_FORWARDED_HEADERS=yes`
- **THEN** в журнале ошибка про `TRUST_FORWARDED_HEADERS`, процесс завершается с кодом `1`

#### Scenario: Переменная не задана
- **WHEN** гейтвей стартует без `TRUST_FORWARDED_HEADERS`
- **THEN** доверия к присланным заголовкам нет

### Requirement: Схема исходного запроса
Гейтвей SHALL передавать апстриму `X-Forwarded-Proto`. Без доверия значением
SHALL быть `http`, а присланный клиентом `X-Forwarded-Proto` SHALL
отбрасываться. С доверием гейтвей SHALL передавать присланное значение, если
оно `http` или `https` (без учёта регистра), иначе `http`.

#### Scenario: Клиент подделывает схему
- **WHEN** без доверия клиент шлёт `X-Forwarded-Proto: https` прямо на гейтвей
- **THEN** google-auth получает `X-Forwarded-Proto: http`

#### Scenario: Запрос через TLS-прокси
- **WHEN** с доверием TLS-прокси шлёт `X-Forwarded-Proto: https`
- **THEN** google-auth получает `X-Forwarded-Proto: https`

#### Scenario: Мусорное значение
- **WHEN** с доверием приходит `X-Forwarded-Proto: gopher`
- **THEN** апстрим получает `X-Forwarded-Proto: http`

### Requirement: Адрес клиента
Гейтвей SHALL передавать апстриму `X-Forwarded-For`. Без доверия значением
SHALL быть адрес сетевого соединения с гейтвеем, а присланный `X-Forwarded-For`
отбрасывается. С доверием присланное значение SHALL передаваться без изменений
(первый адрес в нём — клиент), а при отсутствии заголовка значением SHALL быть
адрес соединения.

#### Scenario: Клиент подделывает адрес
- **WHEN** без доверия клиент шлёт `X-Forwarded-For: 10.9.9.9`
- **THEN** апстрим получает `X-Forwarded-For` с адресом соединения клиента, а не `10.9.9.9`

#### Scenario: Запрос через прокси
- **WHEN** с доверием прокси шлёт `X-Forwarded-For: 192.168.1.50`
- **THEN** апстрим получает `X-Forwarded-For: 192.168.1.50`

#### Scenario: Нет заголовка при доверии
- **WHEN** с доверием запрос пришёл без `X-Forwarded-For`
- **THEN** апстрим получает адрес соединения
