#include "versions.hpp"

#include <ctime>
#include <future>

#include "docs.hpp"
#include "nlohmann/json.hpp"

namespace versions {
namespace {

// Секунда на соединение и секунда на чтение: зависший сервис задерживает
// сводку не больше чем на ~2 с, а не на дефолтные таймауты httplib (5 с на
// соединение, 300 с на чтение).
constexpr time_t kConnectTimeoutSec = 1;
constexpr time_t kReadTimeoutSec = 1;

nlohmann::json failed(const std::string& service, const char* reason) {
    return {{"service", service}, {"error", reason}};
}

// Ответ /version апстрима — как есть, без переименования полей (старые
// сборки без build/started_at тоже проходят: веб покажет «—»).
nlohmann::json fetch_version(const Target& t) {
    httplib::Client cli(t.base_url);
    cli.set_connection_timeout(kConnectTimeoutSec, 0);
    cli.set_read_timeout(kReadTimeoutSec, 0);
    cli.set_write_timeout(kReadTimeoutSec, 0);
    auto r = cli.Get("/version");
    if (!r) {
        httplib::Error e = r.error();
        bool timeout = e == httplib::Error::ConnectionTimeout || e == httplib::Error::Timeout ||
                       e == httplib::Error::Read;
        return failed(t.service, timeout ? "timeout" : "unreachable");
    }
    if (r->status != 200) return failed(t.service, "bad_response");
    nlohmann::json body = nlohmann::json::parse(r->body, nullptr, /*allow_exceptions=*/false);
    if (!body.is_object()) return failed(t.service, "bad_response");
    return body;
}

std::string now_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

} // namespace

void register_route(httplib::Server& svr, std::vector<Target> targets) {
    svr.Get("/api/versions", [targets = std::move(targets)](const httplib::Request&, httplib::Response& res) {
        // Все запросы — сразу, ждём их вместе: время ответа — самый медленный
        // сервис, а не сумма.
        std::vector<std::future<nlohmann::json>> pending;
        pending.reserve(targets.size());
        for (const Target& t : targets) {
            pending.push_back(std::async(std::launch::async, fetch_version, t));
        }

        nlohmann::json services = nlohmann::json::array();
        services.push_back(docsapi::version_json());
        for (auto& f : pending) services.push_back(f.get());

        res.set_header("Cache-Control", "no-store");
        res.set_content(nlohmann::json{{"checked_at", now_iso()}, {"services", std::move(services)}}.dump(),
                        "application/json; charset=utf-8");
    });
}

} // namespace versions
