// Состояние процесса wallet-gateway — по сути только таблица маршрутизации
// плюс публичные ключи auth-сервиса для проверки JWT на защищённых
// префиксах (см. main.cpp).
#pragma once

#include <memory>
#include <string>

#include "util/jwks.hpp"

struct AppState {
    // Публичные ключи auth-сервиса (RS256, GET /.well-known/jwks.json) —
    // используются ТОЛЬКО чтобы отсечь заведомо нерабочий токен
    // (нет/просрочен/битая подпись) на входе в защищённые префиксы (см.
    // find_route в main.cpp), не более того. Финальное решение "можно ли
    // этому пользователю то, что он просит" по-прежнему принимает сам
    // бекенд (см. README.md — почему гейтвей не дублирует авторизацию, а
    // только валидирует токен). Никакого общего секрета с auth-сервисом
    // не заводим — только его публичный ключ.
    std::unique_ptr<jwks::Cache> jwks;

    // Куда роутить /auth/*, /admin/* — google-auth.
    std::string auth_service_url;

    // Куда роутить /api/* (кроме /api/reports/*, см. find_route в main.cpp) —
    // отдельный сервис со своей БД, см. его README.
    std::string wallet_api_url;

    // Куда роутить /api/reports/* — вынесенный CQRS-lite query-сервис,
    // единственная read-only роль на ТОЙ ЖЕ базе, что у wallet_api_url (см.
    // README wallet-report, "Шареная БД, разные роли"). Проверяется ДО
    // общего префикса /api/* в find_route — иначе тот перехватил бы эти
    // пути первым (порядок регистрации важен, см. find_route).
    std::string wallet_report_api_url;

    // Куда роутить /api/ai/* — LLM-функции (org/docs/TODO_wallet_ai.md).
    // Тоже ДО общего /api/* в find_route.
    std::string wallet_ai_url;

    // Куда роутить /api/mcp/* — MCP-сервер для LLM-клиентов (wallet-mcp).
    // Тоже ДО общего /api/* в find_route, и единственный префикс внутри
    // /api/*, где гейтвей НЕ проверяет токен: клиент предъявляет
    // непрозрачный wmcp_…, его проверяет сам wallet-mcp.
    std::string wallet_mcp_url;

    // Куда роутить всё остальное (фолбэк по умолчанию) — статика SPA,
    // отдаёт nginx. Раньше здесь был wallet-bff (сервис упразднён при
    // переходе на SPA — см. README wallet-web, "Куда делся wallet-bff").
    std::string wallet_web_url;

    // wallet-telegram — НЕ маршрут: только адрес для опроса его /version в
    // сводке GET /api/versions. Пусто (WALLET_TELEGRAM_URL не задан) — бота
    // в сводке нет.
    std::string wallet_telegram_url;
};
