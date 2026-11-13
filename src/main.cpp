// wallet-gateway — единая точка входа для браузера: снаружи один origin
// (один хост:порт), внутри — маршрутизация по префиксу пути на настоящие
// сервисы (auth-service, wallet-api, wallet-web). Решает ту же
// задачу, что в проде решит AWS ALB с path-based routing (см. README.md) —
// именно поэтому cookie "session"/"refresh_token", выставленные
// auth-сервисом без явного Domain, одинаково видны везде: с точки зрения
// браузера это всегда один и тот же хост.
//
// Дополнительно: на защищённых префиксах (см. find_route) гейтвей сам
// проверяет подпись и срок годности access-токена — как edge JWT authorizer
// в API Gateway/ALB — и не пропускает запрос дальше, если токена нет или он
// нерабочий (handle_pre_routing). Это ТОЛЬКО грубый фильтр "токен вообще
// настоящий"; авторизацию (какая роль что может) по-прежнему делает сам
// бекенд — см. README.md, "Почему не дублируем авторизацию". Сам проксинг
// (handle_proxy) — отдельно, через catch-all Get/Post/... хендлеры, а не
// в pre_routing_handler — см. комментарий у handle_proxy почему.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "app_state.hpp"
#include "docs.hpp"
#include "httplib.h"
#include "util/dotenv.hpp"
#include "util/jwks.hpp"
#include "util/jwt.hpp"
#include "util/jwt_claims.hpp"
#include "util/log.hpp"
#include "util/metrics.hpp"
#include "version.hpp"
#include "versions.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
void fix_console_encoding() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}
#else
void fix_console_encoding() {}
#endif

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

std::string env_required(const char* key, const std::string& hint) {
    const char* v = std::getenv(key);
    if (v == nullptr || *v == '\0') {
        applog::error(std::string(key) + " must be set — " + hint + " (см. README.md)");
        std::exit(1);
    }
    return std::string(v);
}

std::string strip_trailing_slash(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

// Не льём в лог мегабайтные тела целиком (гейтвей проксирует всё как есть,
// включая blob из wallet-api, см. blob.cpp) — обрезаем и явно отмечаем факт
// обрезки в самой строке, а не молча теряем хвост незамеченным.
//
// Лимит — в СЫРЫХ байтах тела, не в байтах итоговой строки лога: на
// бинарных телах (проксируемый blob) непечатные байты при JSON-экранировании
// раздуваются до 6 символов каждый — при старом лимите 4096 байт это ломало
// journald (резал строку на куски, `CONTAINER_PARTIAL_MESSAGE:true`) и
// fluent-bit (строгий JSON-парсер app_json падал на оборванном куске,
// запись оставалась без level/service/trace_id/msg — только сырой
// "message"), см. подробный разбор в wallet-api/src/main.cpp.
constexpr std::size_t kMaxLoggedBodyBytes = 1024;

std::string truncate_body(const std::string& body) {
    if (body.size() <= kMaxLoggedBodyBytes) return body;
    return body.substr(0, kMaxLoggedBodyBytes) + "...(обрезано, всего " +
           std::to_string(body.size()) + " байт)";
}

// Тело ответа для debug-лога. Сжатые ответы — и те, что сжал сам httplib
// (ДО вызова логгера), и те, что пришли от апстрима уже с Content-Encoding
// и проксируются байт-в-байт, — в лог шли байтами gzip, нечитаемыми.
// Сжатое тело не пишем, только кодировку и размер.
std::string loggable_response_body(const httplib::Response& res) {
    std::string encoding = res.get_header_value("Content-Encoding");
    if (!encoding.empty()) return "(сжато " + encoding + ", " + std::to_string(res.body.size()) + " байт)";
    return truncate_body(res.body);
}

// Тела запросов/ответов в журнал — только по явному LOG_BODIES=1 (локальная
// отладка), а не по уровню debug: debug включён в контурах по умолчанию, и
// раньше тела всех маршрутов — суммы отчётов, /sync/push, ответы /auth/refresh
// с refresh-токеном, ответы инструментов /api/mcp/ — уходили в журнал и
// OpenSearch (2026-11-02, test). Без флага пишутся только размеры.
bool g_log_bodies = false;

// Даже с LOG_BODIES=1 эти тела не пишутся никогда: в /auth/* — токены и коды
// входа, в /api/mcp/ — ответы инструментов с суммами и комментариями (design
// D13 wallet-mcp: тела MCP в журнал не попадают).
bool body_logging_forbidden(const std::string& path) {
    auto starts = [&](const char* p) { return path.rfind(p, 0) == 0; };
    return path == "/auth" || starts("/auth/") || path == "/api/mcp" || starts("/api/mcp/");
}

std::string default_log_dir() {
#ifdef _WIN32
    return "logs";
#else
    return "";
#endif
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with(const std::string& path, const char* prefix) {
    return path.rfind(prefix, 0) == 0;
}

struct RouteTarget {
    std::string upstream;
    bool require_jwt;
    // Грубая категория для applog::set_route (см. log.hpp) — конкретную
    // ручку (типа "DELETE /api/wallets/:id") знает только сам апстрим,
    // гейтвей видит лишь префикс пути и решает, куда проксировать; этого
    // достаточно, чтобы отдельно видеть latency самого проксирования
    // (сеть + грубый JWT-гейт) по категориям, не смешивая /api/* с
    // статикой SPA в одном перцентиле.
    std::string route_label;
    // aud, который обязан быть в токене для этого маршрута — имя бэкенда
    // (org/docs/TODO_telegram_tokens.md, шаг 2). Пусто — маршрут без JWT-гейта.
    // scope гейтвей не проверяет: что именно можно, решает сам бэкенд.
    std::string audience;
};

// Порядок важен — первый подходящий префикс побеждает. /auth/* и /admin/*
// НЕ защищены здесь намеренно:
//  - /auth/* — сам логин/refresh/logout, там по определению ещё нет (или
//    уже не нужен) валидного access-токена; закрыть его на гейтвее значило
//    бы, что никто не смог бы залогиниться вообще.
//  - /admin/* — там уже есть своя логика в admin.cpp (JSON 401/403 —
//    401 неавторизованным, 403 не-админам — тот же приём, что и в
//    wallet-api) — гейт на гейтвее проверяет только подпись+exp, не роль,
//    так что дублировать эту проверку тут значило бы просто продублировать
//    JWT-верификацию, ничего не выиграв в безопасности.
// /api/mcp/* — единственное исключение ВНУТРИ /api/*: MCP-клиент
// (LM Studio и т.п.) предъявляет не JWT, а персональный непрозрачный токен
// wmcp_… (статичный заголовок в mcp.json, обновлять 15-минутный JWT клиент
// не умеет). Гейт отклонил бы его с 401 раньше, чем запрос дошёл бы до
// wallet-mcp, а учить гейтвей форматам токенов конкретного сервиса незачем
// — токен как есть уходит в wallet-mcp, тот сам меняет его на короткий JWT
// в google-auth и отвечает 401 на всё без токена.
// /api/* — единственное место, где грубая проверка на входе оправдана:
// это чистый JSON REST, 401 тут — штатный, ожидаемый ответ (см.
// wallet::require_auth в wallet-api — гейтвей просто отсекает то же
// самое чуть раньше, до похода в сеть до бекенда). Раньше /api/* вёл на
// auth-сервис (там же временно жил CRUD кошельков) — теперь на wallet-api,
// отдельный сервис со своей БД (см. его README).
RouteTarget find_route(const AppState& state, const std::string& path) {
    // /api/reports/* — ДО общего /api/* ниже: CQRS-lite query-сервис,
    // вынесенный из wallet-api (см. app_state.hpp) — тот же грубый JWT-гейт,
    // сам wallet-report разбирается дальше (резолв леджера пользователя).
    if (starts_with(path, "/api/reports/")) {
        return {state.wallet_report_api_url, true, "proxy /api/reports/*", "wallet-report"};
    }
    // /api/ai/* — тоже ДО общего /api/*: LLM-функции (wallet-ai, см.
    // org/docs/TODO_wallet_ai.md). scope ai:use проверяет сам wallet-ai.
    if (starts_with(path, "/api/ai/")) return {state.wallet_ai_url, true, "proxy /api/ai/*", "wallet-ai"};
    // /api/docs/* — тоже ДО общего /api/*: справочник по спекам (wallet-docs,
    // change docs-route). Область документации (role, platform) проверяет сам
    // wallet-docs. Пустой wallet_docs_url сюда не доходит — handle_pre_routing
    // отвечает 404 раньше. /api/docs без слеша уходит в общий /api/.
    if (starts_with(path, "/api/docs/")) {
        return {state.wallet_docs_url, true, "proxy /api/docs/*", "wallet-docs"};
    }
    // /api/mcp/* — ДО общего /api/* и БЕЗ гейта (require_jwt = false, aud
    // пуст), см. комментарий над find_route. /api/mcp без слеша сюда не
    // попадает — уходит в общий /api/ и получает 401 гейта.
    if (starts_with(path, "/api/mcp/")) return {state.wallet_mcp_url, false, "proxy /api/mcp/*"};
    if (starts_with(path, "/api/")) return {state.wallet_api_url, true, "proxy /api/*", "wallet-api"};
    // /sync/* — POST /sync/push, GET /sync/pull (org/docs/TODO_mobile_sync.md,
    // Фаза 2): тот же upstream, что и /api/*, тот же грубый JWT-гейт (сам
    // wallet-api разбирается дальше, только он ещё и проверяет claim
    // device_id — см. sync.cpp, authenticate_and_provision).
    if (starts_with(path, "/sync/")) return {state.wallet_api_url, true, "proxy /sync/*", "wallet-api"};
    if (starts_with(path, "/auth/")) return {state.auth_service_url, false, "proxy /auth/*"};
    if (starts_with(path, "/admin/")) return {state.auth_service_url, false, "proxy /admin/*"};
    // Фолбэк по умолчанию — статика SPA (nginx, см. README wallet-web).
    // Раньше здесь был wallet-bff — сервис упразднён, см. README wallet-web,
    // "Куда делся wallet-bff".
    return {state.wallet_web_url, false, "proxy (static)"};
}

// Тот же порядок проверки, что в auth::authenticate() google-auth:
// сначала Authorization: Bearer (для curl/Postman/будущих API-клиентов),
// иначе cookie "session" (для браузера).
std::string extract_bearer(const httplib::Request& req) {
    std::string header = req.get_header_value("Authorization");
    static const std::string prefix = "Bearer ";
    if (header.size() > prefix.size() && header.compare(0, prefix.size(), prefix) == 0) {
        return header.substr(prefix.size());
    }
    return {};
}

std::string extract_cookie(const httplib::Request& req, const std::string& name) {
    std::string header = req.get_header_value("Cookie");
    size_t pos = 0;
    while (pos <= header.size()) {
        size_t semi = header.find(';', pos);
        std::string piece =
            header.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);

        size_t key_start = piece.find_first_not_of(' ');
        if (key_start != std::string::npos) {
            size_t eq = piece.find('=', key_start);
            if (eq != std::string::npos) {
                std::string key = piece.substr(key_start, eq - key_start);
                if (key == name) return piece.substr(eq + 1);
            }
        }

        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return {};
}

// Тело 401 JWT-гейта — одно для проксируемых путей и для /api/versions.
constexpr const char* kUnauthorizedBody =
    "gateway: access-токен отсутствует, просрочен или подпись не совпала — "
    "войдите через /auth/google (или обновите его через /auth/refresh) и повторите запрос";

// payload_out — проверенный payload токена, если он нужен вызывающему (роль
// для собственного пути GET /api/versions); для проксируемых путей не нужен.
bool has_valid_token(const httplib::Request& req, jwks::Cache& jwks_cache, const std::string& audience,
                     nlohmann::json* payload_out = nullptr) {
    std::string token = extract_bearer(req);
    if (token.empty()) token = extract_cookie(req, "session");
    if (token.empty()) return false;

    auto kid = jwt::peek_kid(token);
    if (!kid) return false;
    auto key = jwks_cache.find(*kid);
    if (!key) return false;

    auto payload = jwt::verify_rs256(token, key->n, key->e);
    if (!payload) return false;

    // iss/aud (org/docs/TODO_telegram_tokens.md, шаг 2): токен должен быть
    // адресован бэкенду маршрута, иначе, например, токен только для
    // wallet-report прошёл бы гейт на /sync/*. Токен без aud — старый, с
    // шага 3 отклоняется.
    jwt_claims::Result claims = jwt_claims::check(*payload, audience, "");
    metrics::inc_counter("jwt_claims_total", "Проверки iss/aud access-токена гейтвеем по результату.", "result",
                         jwt_claims::to_string(claims));
    if (claims != jwt_claims::Result::Ok) {
        applog::warn("access token rejected",
                     {{"event", "jwt_rejected"}, {"reason", jwt_claims::to_string(claims)}, {"audience", audience}});
        return false;
    }

    // Токен проверен — дальнейшие строки лога этого запроса (в т.ч.
    // финальная request_finished) сами унаследуют user_id (см.
    // applog::set_user в log.hpp). Гейтвей тут — только грубый фильтр (см.
    // комментарий вверху файла), поэтому просто читаем "sub" как есть, без
    // try/catch — value() ниже не бросает, вернёт пустую строку, если поля
    // нет.
    applog::set_user(payload->value("sub", std::string{}));
    if (payload_out != nullptr) *payload_out = std::move(*payload);
    return true;
}

// Заголовки, которые нельзя слепо перекладывать между hop'ами прокси:
// Host — иначе апстрим получит хост гейтвея вместо своего; Content-Length/
// Connection/Transfer-Encoding — httplib сам пересчитает их корректно и на
// исходящем запросе (по факту req.body), и на ответе (по факту res.body),
// форвардинг старых значений один-в-один может им противоречить.
bool is_hop_by_hop(const std::string& key) {
    std::string k = lowercase(key);
    return k == "host" || k == "content-length" || k == "connection" ||
           k == "transfer-encoding";
}

// Accept-Encoding НЕ форвардится апстриму как есть (см. её единственное
// место использования ниже) — по прямому запросу ("правь все пункты по
// нагрузке", gzip на /api/reports/*). Апстримы (wallet-report/wallet-api)
// сами умеют сжимать ответ (свой CPPHTTPLIB_ZLIB_SUPPORT), но декомпрессия
// НЕ нужна ни на этом хопе (гейтвей -> апстрим — один хост/сеть compose,
// экономить там пропускную способность сжатием незачем), ни, что важнее,
// вообще где-либо в этом бинарнике: cpp-httplib::Client, ЕСЛИ у него самого
// собран zlib (см. CMakeLists.txt — здесь собран, ради сжатия НАРУЖУ к
// браузеру самим гейтвеем, см. ниже), сам ДОПИСЫВАЕТ заголовок
// "Accept-Encoding: gzip, deflate" на исходящий запрос, если этого заголовка
// нет ВООБЩЕ (см. httplib.h, ClientImpl::send — не спрашивая явно, "хочет"
// ли вызывающий код сжатый ответ) — просто не форвардить браузерный
// Accept-Encoding тут НЕДОСТАТОЧНО: апстрим всё равно сожмёт по
// подставленному самим клиентом умолчанию, и ЭТОТ клиент его благополучно
// разожмёт... а его Content-Encoding-заголовок (уже неактуальный для
// РАЗЖАТОГО тела) forward-цикл ниже утащил бы дальше браузеру как есть —
// ровно так однажды и утёк "Content-Encoding: gzip" поверх на самом деле
// голого JSON (см. историю чата). Явный "identity" — единственный способ
// сказать апстриму "не сжимай" и одновременно не позволить Client
// подставить свой умолчательный gzip вместо пустоты.
bool is_accept_encoding(const std::string& key) { return lowercase(key) == "accept-encoding"; }
constexpr const char* kNoCompressionUpstream = "identity";

// req.target — «сырой» путь запроса ВКЛЮЧАЯ query string (в отличие от
// req.path, который httplib уже разбирает отдельно от параметров) — как
// раз то, что нужно передать апстриму без пересборки.
bool proxy_request(const std::string& upstream, const httplib::Request& req,
                    httplib::Response& res) {
    httplib::Client cli(upstream);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(30, 0);

    httplib::Request out;
    out.method = req.method;
    out.path = req.target;
    out.body = req.body;
    for (const auto& [key, value] : req.headers) {
        if (!is_hop_by_hop(key) && !is_accept_encoding(key)) out.headers.emplace(key, value);
    }
    // Явный "identity", не просто отсутствие заголовка — см. комментарий у
    // is_accept_encoding выше, почему пустое место здесь подставил бы сам
    // httplib::Client своим умолчательным "gzip, deflate".
    out.headers.emplace("Accept-Encoding", kNoCompressionUpstream);
    // Host выше сознательно не форвардится (см. is_hop_by_hop) — апстрим сам
    // не может отличить, каким адресом реально пользовался браузер
    // (localhost:8090 или LAN-адрес). google-auth это нужно знать, чтобы
    // выбрать правильный redirect_uri для Google (см. его
    // google.cpp::resolve_redirect_uri) — кладём исходный Host отдельным
    // заголовком, стандартным для реверс-прокси именем.
    std::string original_host = req.get_header_value("Host");
    if (!original_host.empty()) out.headers.emplace("X-Forwarded-Host", original_host);
    // Если клиент уже прислал X-Trace-Id, он уже скопирован циклом выше
    // (не hop-by-hop) — здесь только достраиваем случай, когда его не было
    // и gateway сам сгенерировал id в handle_pre_routing (см. main()):
    // апстрим должен увидеть тот же id, что уже попал в наши собственные
    // логи этого запроса (applog::current_trace_id() — тот же thread_local,
    // что выставлен ниже по стеку вызовов на этом же потоке).
    if (out.headers.find("X-Trace-Id") == out.headers.end()) {
        std::string trace_id = applog::current_trace_id();
        if (!trace_id.empty()) out.headers.emplace("X-Trace-Id", trace_id);
    }

    httplib::Response upstream_res;
    httplib::Error err;
    if (!cli.send(out, upstream_res, err)) {
        applog::error("upstream " + upstream + " недоступен (" + httplib::to_string(err) +
                      ") для " + req.method + " " + req.path);
        return false;
    }

    res.status = upstream_res.status;
    for (const auto& [key, value] : upstream_res.headers) {
        if (!is_hop_by_hop(key)) res.set_header(key, value);
    }
    res.body = upstream_res.body; // Content-Length пересчитает сам httplib

    return true;
}

// Достаёт trace_id из входящего запроса (сквозной id уже был выставлен
// где-то раньше — другим сервисом, или его же самим gateway при повторном
// вызове через curl) либо порождает новый — gateway единственная точка
// входа для браузера, поэтому именно здесь, а не глубже в цепочке, id либо
// подхватывается, либо впервые появляется на свет (см. new_trace_id в
// log.hpp почему только тут). Кладёт его в thread_local (applog) на время
// обработки запроса И в заголовок ответа — вручную сопоставить curl-запрос
// со строками в логе, не расковыривая тело ответа.
std::string ensure_trace_id(const httplib::Request& req, httplib::Response& res) {
    std::string trace_id = req.get_header_value("X-Trace-Id");
    if (trace_id.empty()) trace_id = applog::new_trace_id();
    applog::set_trace_id(trace_id);
    res.set_header("X-Trace-Id", trace_id);
    return trace_id;
}

// ТОЛЬКО JWT-гейт — сам проксинг сюда специально не входит, см. её вызов в
// main() и комментарий там же про то, почему.
httplib::Server::HandlerResponse handle_pre_routing(AppState& state, const httplib::Request& req,
                                                     httplib::Response& res) {
    applog::start_request_timer(); // как можно раньше — считаем весь запрос, не только роутинг
    ensure_trace_id(req, res);

    // Собственный путь гейтвея внутри /api/ — сводка версий (versions.cpp).
    // Тот же JWT-гейт, что у /api/* (aud wallet-api), плюс роль admin: у пути
    // нет апстрима, который проверил бы её сам. Прошёл — Unhandled, дальше
    // отвечает svr.Get("/api/versions"), зарегистрированный до catch-all.
    // Остальные методы на этом пути идут по общей таблице (в wallet-api).
    if (req.path == "/api/versions" && (req.method == "GET" || req.method == "HEAD")) {
        applog::set_route("GET /api/versions");
        nlohmann::json payload;
        if (!has_valid_token(req, *state.jwks, "wallet-api", &payload)) {
            res.status = 401;
            res.set_content(kUnauthorizedBody, "text/plain; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (payload.value("role", std::string{}) != "admin") {
            res.status = 403;
            res.set_content("gateway: сводка версий доступна только роли admin", "text/plain; charset=utf-8");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    }

    // wallet-docs — необязательный модуль (change docs-route, D1/D2): без
    // WALLET_DOCS_URL один и тот же 404 любому запросу на /api/docs/*, без
    // проверки токена — анонимный клиент тоже должен увидеть «модуля нет»,
    // а не 401.
    if (state.wallet_docs_url.empty() && starts_with(req.path, "/api/docs/")) {
        applog::set_route("proxy /api/docs/*");
        res.status = 404;
        res.set_content("gateway: docs не подключён", "text/plain; charset=utf-8");
        return httplib::Server::HandlerResponse::Handled;
    }

    RouteTarget route = find_route(state, req.path);
    applog::set_route(route.route_label); // грубая категория — см. RouteTarget/find_route выше
    if (!route.require_jwt || has_valid_token(req, *state.jwks, route.audience)) {
        return httplib::Server::HandlerResponse::Unhandled; // пропускаем дальше, в handle_proxy
    }

    res.status = 401;
    res.set_content(kUnauthorizedBody, "text/plain; charset=utf-8");
    return httplib::Server::HandlerResponse::Handled;
}

// Сам проксинг. НЕ вызывается из pre_routing_handler (см. main()) —
// httplib читает тело запроса только для реально сматченного маршрута
// (Get/Post/... handlers ниже), а pre_routing_handler отрабатывает РАНЬШЕ
// этого чтения (нарочно, чтобы отказ по авторизации не заставлял буферизовать
// потенциально большое тело) — если бы проксинг с `out.body = req.body`
// делался прямо в pre_routing_handler, req.body был бы всегда пустой
// независимо от метода: POST/PUT/PATCH с реальным телом (например,
// POST /api/wallets) долетали бы до апстрима с пустым body, а тот отвечал
// бы 400 "некорректный JSON" — этот баг был здесь до того, как через гейтвей
// пошли реальные POST с телом (см. README, "Через гейтвей, не напрямую" в
// google-auth — раньше этот путь не проверялся, страница была на :8080).
void handle_proxy(AppState& state, const httplib::Request& req, httplib::Response& res) {
    RouteTarget route = find_route(state, req.path);
    if (!proxy_request(route.upstream, req, res)) {
        res.status = 502;
        res.set_content("gateway: сервис недоступен", "text/plain; charset=utf-8");
    }
}

} // namespace

int main() {
    fix_console_encoding();
    dotenv::load(".env");

    std::string log_dir = env_or("LOG_DIR", default_log_dir());
    if (log_dir == "stdout" || log_dir == "-") log_dir.clear();
    applog::init(log_dir, "wallet-gateway");
    g_log_bodies = env_or("LOG_BODIES", "") == "1";

    // build_info для /metrics (см. util/metrics.cpp) — один раз здесь, до
    // svr.listen() (см. GET /version, docs.cpp — та же тройка полей).
    metrics::set_build_info(version::kService, version::kVersion, version::kBuild,
                            version::kGitCommit);

    AppState state;
    state.auth_service_url = strip_trailing_slash(
        env_required("AUTH_SERVICE_URL", "адрес auth-сервиса, например http://127.0.0.1:8080"));
    state.wallet_api_url = strip_trailing_slash(
        env_required("WALLET_API_URL", "адрес wallet-api, например http://127.0.0.1:8082"));
    state.wallet_report_api_url = strip_trailing_slash(
        env_required("WALLET_REPORT_API_URL", "адрес wallet-report, например http://127.0.0.1:8084"));
    state.wallet_ai_url = strip_trailing_slash(
        env_required("WALLET_AI_URL", "адрес wallet-ai, например http://127.0.0.1:8087"));
    state.wallet_mcp_url = strip_trailing_slash(
        env_required("WALLET_MCP_URL", "адрес wallet-mcp, например http://127.0.0.1:8088"));
    state.wallet_web_url = strip_trailing_slash(
        env_required("WALLET_WEB_URL", "адрес wallet-web (nginx), например http://127.0.0.1:8083"));
    // Необязательный: только для сводки GET /api/versions. В find_route бота
    // нет — через гейтвей он не публикуется.
    state.wallet_telegram_url = strip_trailing_slash(env_or("WALLET_TELEGRAM_URL", ""));
    // Необязательный (change docs-route, D1): пусто — /api/docs/* отвечает
    // 404 «gateway: docs не подключён», остальные маршруты как обычно.
    state.wallet_docs_url = strip_trailing_slash(env_or("WALLET_DOCS_URL", ""));
    // Тянет JWKS сразу при старте — если auth-сервис ещё не поднят, не
    // фатально: залогируется ошибка, первый реальный запрос на защищённый
    // префикс попробует обновить кэш ещё раз (см. jwks::Cache::find).
    state.jwks = std::make_unique<jwks::Cache>(state.auth_service_url);

    std::string host = env_or("HOST", "127.0.0.1");
    int port = std::stoi(env_or("PORT", "8090"));

    httplib::Server svr;
    // pre_routing_handler — ТОЛЬКО грубый JWT-гейт, отвечает сам (Handled)
    // лишь при 401; иначе Unhandled — пропускает запрос дальше, в один из
    // catch-all Get/Post/.../handler'ов ниже (паттерн ".*" матчит любой
    // путь). Раньше здесь же, в pre_routing_handler, делался и сам проксинг
    // — но тот вызывается ДО того, как httplib читает тело запроса, поэтому
    // POST/PUT/PATCH с реальным телом всегда проксировались с пустым body
    // (см. комментарий у handle_proxy). Обычные же per-method handlers
    // httplib вызывает уже ПОСЛЕ чтения тела — то, что нужно.
    svr.set_pre_routing_handler([&state](const httplib::Request& req, httplib::Response& res) {
        return handle_pre_routing(state, req, res);
    });

    // ДО catch-all Get(".*", proxy) ниже — иначе тот перехватил бы /docs,
    // /openapi.yaml и статику Swagger UI раньше, чем httplib успел бы
    // попробовать более специфичный маршрут (httplib матчит зарегистрированные
    // маршруты в порядке регистрации, первый подошедший побеждает — см.
    // README, "Порядок важен" у find_route). /openapi.yaml — собственная
    // спека ГЕЙТВЕЯ (маршрутизация + грубый JWT-гейт); спеки апстримов —
    // /openapi/<name>.yaml, в /docs все вместе выпадающим списком.
    // GET /openapi.yaml, /openapi/<name>.yaml, /docs, /healthz, статика /vendor/swagger-ui/*
    docsapi::register_routes(svr, {{"google-auth", state.auth_service_url},
                                   {"wallet-api", state.wallet_api_url},
                                   {"wallet-report", state.wallet_report_api_url},
                                   {"wallet-ai", state.wallet_ai_url},
                                   {"wallet-mcp", state.wallet_mcp_url}});

    // GET /api/versions — тоже до catch-all; доступ проверен в
    // handle_pre_routing. Порядок — порядок строк в сводке.
    std::vector<versions::Target> version_targets = {{"google-auth", state.auth_service_url},
                                                     {"wallet-api", state.wallet_api_url},
                                                     {"wallet-report", state.wallet_report_api_url},
                                                     {"wallet-ai", state.wallet_ai_url},
                                                     {"wallet-mcp", state.wallet_mcp_url}};
    if (!state.wallet_telegram_url.empty()) {
        version_targets.push_back({"wallet-telegram", state.wallet_telegram_url});
    }
    version_targets.push_back({"wallet-web", state.wallet_web_url});
    versions::register_route(svr, std::move(version_targets));

    auto proxy = [&state](const httplib::Request& req, httplib::Response& res) {
        handle_proxy(state, req, res);
    };
    svr.Get(".*", proxy);
    svr.Post(".*", proxy);
    svr.Put(".*", proxy);
    svr.Patch(".*", proxy);
    svr.Delete(".*", proxy);
    svr.Options(".*", proxy);

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        // Метрики — на ВСЕ запросы, включая /healthz и сам /metrics: в
        // отличие от access-лога ниже, кардинальность здесь уже ограничена
        // applog::current_route() (см. util/metrics.hpp) — тем же грубым
        // route_label, что find_route/handle_pre_routing выставляют через
        // applog::set_route() ("proxy /api/*", "proxy (static)" и т.п., см.
        // выше), а знать RPS/латентность healthcheck'а тоже полезно
        // (отличить "не отвечает вовсе" от "отвечает медленно"). "(unset)" —
        // на случай хендлера, который set_route() не вызывает (докстраница,
        // /healthz, /metrics).
        metrics::observe_http_request(
            req.method, applog::current_route().empty() ? "(unset)" : applog::current_route(),
            res.status, applog::request_duration_ms());

        // /healthz — не запрос приложения, а probe compose'а (interval 10s,
        // см. compose.yaml) — по логам он не отличим от реального трафика,
        // но забивает их шумом (~8640 строк/сутки) без всякой пользы: сама
        // проверка ничего не открывает и не пишет. clear_request_context()
        // всё равно нужен (thread_local не должен утечь в следующий, никак
        // не связанный запрос на этом потоке) — выходим, не пропуская его,
        // а не просто пропускаем обе строки лога.
        // /metrics — то же самое: скрейп Prometheus каждые 15 с, в теле —
        // текст метрик (или gzip), для разбора инцидентов бесполезный.
        if (req.path == "/healthz" || req.path == "/metrics") {
            applog::clear_request_context();
            return;
        }
        // Всё ещё на том же потоке, что весь остальной обработчик запроса —
        // trace_id, выставленный в handle_pre_routing, ещё в thread_local,
        // так что эта строка тоже попадёт в трейс. clear_trace_id() —
        // ПОСЛЕДНЕЕ, что касается trace_id на этом потоке: он же дальше уйдёт
        // в пул httplib и обслужит следующий, никак не связанный запрос (см.
        // комментарий у set_trace_id в log.hpp).
        // "event":"request_finished" — машиночитаемая метка ИМЕННО этой
        // строки (не любой другой info/error) — чтобы в Dashboards
        // агрегировать перцентиль/среднее по duration_ms надёжно, а не
        // полагаться на разбор msg (он меняется на каждый вызов: другой
        // path/status).
        applog::info(req.method + " " + req.path + " -> " + std::to_string(res.status),
                     {{"event", "request_finished"}, {"duration_ms", applog::request_duration_ms()}});
        // Отдельной debug-строкой, не полями к строке выше — "request_finished"
        // агрегируют по duration_ms для метрик, подмешивать туда тела запроса/
        // ответа значило бы тащить их в тот же индекс/дашборд. Уровень именно
        // "debug" (см. log.hpp) — тело проксируется как есть и может нести
        // что угодно из апстримов (пароли, токены, платёжные данные), а
        // level — как раз то поле, по которому такие строки отфильтровывают
        // из постоянного мониторинга (jq/Loki), не разбирая msg.
        if (g_log_bodies && !body_logging_forbidden(req.path)) {
            applog::debug(req.method + " " + req.path,
                          {{"event", "request_body"},
                           {"request_body", truncate_body(req.body)},
                           {"response_body", loggable_response_body(res)}});
        } else {
            applog::debug(req.method + " " + req.path,
                          {{"event", "request_body"},
                           {"request_bytes", req.body.size()},
                           {"response_bytes", res.body.size()}});
        }
        applog::clear_request_context();
    });

    applog::info("wallet-gateway v" + std::string(version::kBuild) + " (" +
                 std::string(version::kGitCommit) + ") listening on http://" + host + ":" +
                 std::to_string(port) +
                 " (auth: " + state.auth_service_url + ", wallet-api: " + state.wallet_api_url +
                 ", wallet-report: " + state.wallet_report_api_url +
                 ", wallet-ai: " + state.wallet_ai_url + ", wallet-mcp: " + state.wallet_mcp_url +
                 ", wallet-docs: " + (state.wallet_docs_url.empty() ? std::string("не подключён") : state.wallet_docs_url) +
                 ", web: " + state.wallet_web_url +
                 (state.wallet_telegram_url.empty() ? "" : ", telegram (только версия): " + state.wallet_telegram_url) +
                 ")" +
                 (log_dir.empty() ? "" : " (логи: " + log_dir + "/)"));
    if (!svr.listen(host, port)) {
        applog::error("не удалось запустить сервер на " + host + ":" + std::to_string(port) +
                      " (порт занят или недоступен?)");
        return 1;
    }
    return 0;
}
