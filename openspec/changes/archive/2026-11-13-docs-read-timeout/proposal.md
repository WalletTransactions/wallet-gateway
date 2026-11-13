# Proposal

## Why

На test `POST /api/docs/ask` дважды получил `502 gateway: сервис недоступен` ровно через
30 с (`Failed to read connection`), хотя wallet-docs был жив и уже нашёл фрагменты: он
ждал ответа LLM. Ответ синхронный, LLM на test отвечает за 8–22 с и дольше, а гейтвей
ждёт любой апстрим не дольше 30 с. wallet-docs при этом готов ждать LLM до 120 с
(`LLM_TIMEOUT_MS`). В дизайнах `docs-route` и `docs-service` таймауты не согласованы.

## What Changes

- У маршрута `/api/docs/*` свой таймаут чтения ответа апстрима: по умолчанию **130 с**,
  переменная `WALLET_DOCS_READ_TIMEOUT_SEC` (целое > 0; иначе гейтвей не стартует).
  Остальные маршруты — по-прежнему 30 с, соединение — 10 с.
- Правило согласования: таймаут гейтвея длиннее `LLM_TIMEOUT_MS` wallet-docs, тогда
  долгая LLM даёт клиенту `503 llm_unavailable` wallet-docs, а не `502` гейтвея.
- В журнале старта рядом с адресом wallet-docs — таймаут ожидания.

## Capabilities

### New Capabilities

### Modified Capabilities
- `request-routing`: «Недоступный апстрим» — таймаут ответа зависит от маршрута.
- `proxy-forwarding`: «Ответ буферизуется целиком» — ссылка на таймаут маршрута вместо
  фиксированных 30 с.

## Impact

- **wallet-gateway**: `src/main.cpp`, `src/app_state.hpp`, README, `.env.example`, VERSION.
- **Связь с другими**: wallet-docs `LLM_TIMEOUT_MS` (120 с по умолчанию) должен оставаться
  короче. Пока гейтвей не выкачен, на test `LLM_TIMEOUT_MS=25000` (честный `503`).
- Длинный запрос держит поток гейтвея до 130 с; wallet-docs ограничивает 6 вопросов в
  минуту на пользователя, так что пул потоков это не исчерпает.
