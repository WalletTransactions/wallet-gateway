// GET /api/versions — сводка версий сервисов контура: свой /version гейтвея
// и /version каждого апстрима, опрошенные параллельно по внутренней сети.
// Нужна бейджу версии в вебе и автоматизации тест-задач (openspec/changes/
// service-versions, capability service-versions).
//
// Доступ (валидный токен с aud wallet-api и роль admin) проверяет
// handle_pre_routing в main.cpp — сюда доходят только администраторы.
// Роль гейтвей проверяет, хотя для проксируемых путей это дело апстрима:
// у этого пути апстрима нет, сервис здесь — сам гейтвей (design D4).
#pragma once

#include <string>
#include <vector>

#include "httplib.h"

namespace versions {

// Сервис, чей GET /version попадает в сводку. Порядок вектора — порядок в
// ответе (после самого гейтвея).
struct Target {
    std::string service;
    std::string base_url;
};

void register_route(httplib::Server& svr, std::vector<Target> targets);

} // namespace versions
