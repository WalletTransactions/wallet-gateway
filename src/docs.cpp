#include "docs.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"
#include "util/metrics.hpp"
#include "version.hpp"

namespace docsapi {

namespace {

constexpr const char* kOpenApiPath = "openapi.yaml";
constexpr const char* kVendorDir = "vendor/swagger-ui";

// StandaloneLayout (не BaseLayout) — то же самое, что открыть spec напрямую
// на petstore.swagger.io/docs — с topbar и полем Authorize (нужно, у нас
// bearerAuth/sessionCookie в security, см. components.securitySchemes
// openapi.yaml). tryItOutEnabled — форма сразу готова к вызову, не нужно
// сначала нажимать "Try it out" отдельно на каждом методе.
//
// urls (не url) — выпадающий список спек в topbar: своя гейтвейная первой
// и по умолчанию (urls.primaryName), за ней апстримы (/openapi/<name>.yaml,
// см. handle_upstream_spec). @URLS@ подставляется в register_routes.
constexpr const char* kDocsHtmlTemplate = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>wallet-gateway — API docs</title>
<link rel="stylesheet" href="/vendor/swagger-ui/swagger-ui.css">
<link rel="icon" type="image/png" sizes="32x32" href="/vendor/swagger-ui/favicon-32x32.png">
<link rel="icon" type="image/png" sizes="16x16" href="/vendor/swagger-ui/favicon-16x16.png">
<style>body{margin:0;background:#fafafa}</style>
</head>
<body>
<div id="swagger-ui"></div>
<script src="/vendor/swagger-ui/swagger-ui-bundle.js"></script>
<script src="/vendor/swagger-ui/swagger-ui-standalone-preset.js"></script>
<script>
window.onload = function () {
  window.ui = SwaggerUIBundle({
    urls: @URLS@,
    "urls.primaryName": "wallet-gateway",
    dom_id: "#swagger-ui",
    presets: [SwaggerUIBundle.presets.apis, SwaggerUIStandalonePreset],
    layout: "StandaloneLayout",
    tryItOutEnabled: true
  });
};
</script>
</body>
</html>
)HTML";

void handle_openapi_yaml(const httplib::Request&, httplib::Response& res) {
    std::ifstream f(kOpenApiPath, std::ios::binary);
    if (!f) {
        res.status = 500;
        res.set_content(
            "openapi.yaml не найден рядом с исполняемым файлом — сборка не докопировала его "
            "(см. CMakeLists.txt/Containerfile)",
            "text/plain; charset=utf-8");
        return;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    // "application/yaml" — не станадртизировано так же жёстко, как
    // application/json, но это ровно то, что показывает Swagger UI/браузер
    // без сюрпризов (не пытается открыть "Save as" как для
    // application/octet-stream).
    res.set_content(buf.str(), "application/yaml; charset=utf-8");
}

std::string render_docs_html(const std::vector<UpstreamSpec>& upstreams) {
    nlohmann::json urls = nlohmann::json::array();
    urls.push_back({{"name", "wallet-gateway"}, {"url", "/openapi.yaml"}});
    for (const auto& u : upstreams) {
        urls.push_back({{"name", u.name}, {"url", "/openapi/" + u.name + ".yaml"}});
    }
    std::string html = kDocsHtmlTemplate;
    static const std::string placeholder = "@URLS@";
    html.replace(html.find(placeholder), placeholder.size(), urls.dump());
    return html;
}

// Спека апстрима объявляет только свой прямой адрес (servers:
// http://localhost:808x) — "Try it out" со страницы гейтвея ушёл бы туда
// кросс-доменным запросом, и браузер его зарубил бы по CORS (апстримы CORS
// не отдают). Заменяем весь список на относительный "/" — тот же адрес, с
// которого открыта сама страница, то есть через гейтвей. Прямой адрес не
// оставляем: выбрать его в Servers значило бы гарантированный "Failed to
// fetch". Служебные пути апстрима (/healthz, /version, /.well-known/*,
// /internal/*) гейтвей не проксирует — их пробовать в /docs самого апстрима.
// Текстовая замена, не разбор YAML: у всех апстримов servers: — ключ
// верхнего уровня, элементы с отступом; блок кончается первой строкой без
// отступа (или пустой). Не нашли ключ — отдаём спеку как есть.
std::string replace_servers_with_gateway(std::string yaml) {
    static const std::string key = "\nservers:\n";
    auto pos = yaml.find(key);
    if (pos == std::string::npos) return yaml;
    const auto begin = pos + key.size();
    auto end = begin;
    while (end < yaml.size() && yaml[end] == ' ') {
        auto nl = yaml.find('\n', end);
        end = nl == std::string::npos ? yaml.size() : nl + 1;
    }
    yaml.replace(begin, end - begin,
                 "  - url: /\n"
                 "    description: Через гейтвей — тот же адрес, что у этой страницы\n");
    return yaml;
}

// GET /openapi/<name>.yaml — спека апстрима, забранная у него самого на
// каждый запрос (не копия в образе гейтвея): показывает ровно то, что
// сейчас выкачено, даже если апстрим пересобрали без гейтвея.
void handle_upstream_spec(const std::vector<UpstreamSpec>& upstreams, const httplib::Request& req,
                          httplib::Response& res) {
    const std::string name = req.matches[1];
    auto it = std::find_if(upstreams.begin(), upstreams.end(),
                           [&](const UpstreamSpec& u) { return u.name == name; });
    if (it == upstreams.end()) {
        res.status = 404;
        res.set_content("gateway: нет такого апстрима — " + name, "text/plain; charset=utf-8");
        return;
    }

    httplib::Client cli(it->base_url);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(5, 0);
    auto upstream_res = cli.Get("/openapi.yaml");
    if (!upstream_res || upstream_res->status != 200) {
        res.status = 502;
        res.set_content("gateway: не удалось получить openapi.yaml у " + name + " (" + it->base_url + ")",
                        "text/plain; charset=utf-8");
        return;
    }
    res.set_content(replace_servers_with_gateway(upstream_res->body), "application/yaml; charset=utf-8");
}

// Liveness для docker/podman compose healthcheck (см. wallet-infra/compose.yaml) —
// намеренно НЕ проксирует /healthz апстримов (auth-сервис/wallet-api/
// wallet-web): смысл этой ручки — убедиться, что жив сам гейтвей (его
// httplib-сервер и JWT-гейт), а не вся цепочка целиком — иначе временная
// недоступность одного апстрима триггерила бы restart-политику гейтвея,
// хотя сам он в порядке. Зарегистрирована ДО catch-all Get(".*", proxy) в
// main() — иначе матчился бы он, а не эта ручка (см. комментарий там же).
// Без авторизации — как и /openapi.yaml, /docs.
void handle_healthz(const httplib::Request&, httplib::Response& res) {
    res.set_content(R"({"status":"ok"})", "application/json; charset=utf-8");
}

// GET /metrics — Prometheus text exposition format (см. util/metrics.hpp,
// infra/monitoring/prometheus.yml). Зарегистрирована здесь же, ДО catch-all
// Get(".*", proxy) в main.cpp — иначе матчился бы он, а не эта ручка (см.
// комментарий там же). Без авторизации — как и /healthz: внутренняя ручка
// для скрейпа Prometheus, не часть публичного API.
void handle_metrics(const httplib::Request&, httplib::Response& res) {
    res.set_content(metrics::render(), "text/plain; version=0.0.4; charset=utf-8");
}

// GET /version — версия ЭТОГО конкретного билда (см. CMakeLists.txt, "Версия
// приложения": version.hpp сгенерирован CMake из VERSION + git-хэша на
// момент configure). Зарегистрирована здесь же, ДО catch-all Get(".*", proxy)
// в main.cpp — иначе матчился бы он, а не эта ручка (см. комментарий там же).
// Без авторизации — как и /healthz, /metrics: внутренняя диагностическая
// ручка, не часть публичного API.
void handle_version(const httplib::Request&, httplib::Response& res) {
    nlohmann::json j;
    j["service"] = version::kService;
    j["version"] = version::kVersion;
    j["git_commit"] = version::kGitCommit;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}

} // namespace

void register_routes(httplib::Server& svr, std::vector<UpstreamSpec> upstreams) {
    svr.Get("/openapi.yaml", handle_openapi_yaml);
    // Страница собирается один раз — список апстримов после старта не меняется.
    svr.Get("/docs", [html = render_docs_html(upstreams)](const httplib::Request&, httplib::Response& res) {
        res.set_content(html, "text/html; charset=utf-8");
    });
    svr.Get(R"(/openapi/([a-z0-9-]+)\.yaml)",
            [upstreams = std::move(upstreams)](const httplib::Request& req, httplib::Response& res) {
                handle_upstream_spec(upstreams, req, res);
            });
    svr.Get("/healthz", handle_healthz);
    svr.Get("/metrics", handle_metrics);
    svr.Get("/version", handle_version);
    // set_mount_point отдаёт статику по префиксу пути — здесь это ТОЛЬКО
    // сам Swagger UI (JS/CSS/favicon), не пересекается ни с одним реальным
    // API-путём сервиса (все они начинаются с /api/ или /sync/).
    svr.set_mount_point("/vendor/swagger-ui", kVendorDir);
}

} // namespace docsapi
