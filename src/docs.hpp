// Отдаёт собственную OpenAPI-спеку сервиса (openapi.yaml, "Зеркало
// openapi.yaml следует за API" — правится в том же заходе, что и сам роут,
// см. src/wallet.cpp/sync.cpp/blob.cpp) и Swagger UI поверх неё (GET /docs),
// чтобы описание методов и живую форму для вызовов не нужно было носить
// отдельно от сервиса. Swagger UI — вендоренный статический бандл
// (third_party/swagger-ui/, см. его README — версия/происхождение), не
// CDN-ссылка: сервис должен отдавать документацию и без доступа наружу (см.
// org/docs/TODO_mobile_sync.md, "LAN-only MVP").
//
// Чисто статика, без авторизации — то же самое, что curl openapi.yaml,
// только с интерактивной формой (кнопка Authorize в /docs подставляет
// Authorization: Bearer в реальные вызовы "Try it out").
//
// openapi.yaml и third_party/swagger-ui/ читаются С ДИСКА по относительному
// пути (рабочая директория — папка рядом с исполняемым файлом, та же, где
// .env — см. scripts/windows/run-console.bat) — CMakeLists.txt копирует туда
// оба при каждой сборке (POST_BUILD), Containerfile — при сборке образа.
//
// Гейтвей в /docs показывает не только свою спеку, но и спеки апстримов —
// выпадающий список Swagger UI вверху страницы (см. UpstreamSpec ниже).
#pragma once

#include <string>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace docsapi {

// Апстрим, чья спека доступна в /docs гейтвея: GET /openapi/<name>.yaml
// забирает <base_url>/openapi.yaml по внутренней сети (браузер к апстриму
// напрямую не ходит — ни CORS, ни проброшенный наружу порт не нужны).
struct UpstreamSpec {
    std::string name;
    std::string base_url;
};

void register_routes(httplib::Server& svr, std::vector<UpstreamSpec> upstreams);

// Тело GET /version гейтвея: service, version, build, git_commit, started_at.
// Им же гейтвей открывает сводку GET /api/versions (versions.cpp).
nlohmann::json version_json();

} // namespace docsapi
