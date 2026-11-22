#include "upstream_tls.hpp"

#include <cstdlib>
#include <map>
#include <mutex>

namespace upstream_tls {

namespace {

nettls::Config g_cfg;
std::string g_plain_origin;
std::mutex g_pools_mu;
std::map<std::string, std::unique_ptr<nettls::ClientPool>> g_pools;

// Предел одновременных соединений к одному апстриму (UPSTREAM_POOL_SIZE, по умолчанию 24). Меньше числа
// потоков серверов-апстримов (httplib: от 11 до 44 на 12 ядрах), чтобы каждое соединение пула имело свой
// поток, а лишние запросы ждали в пуле гейтвея по очереди, а не копились свежими рукопожатиями на
// стороне сервера (замер add-internal-mtls, 10.2: 2–3 % SSL connection failed под нагрузкой).
std::size_t pool_limit() {
    const char* v = std::getenv("UPSTREAM_POOL_SIZE");
    if (v && *v) {
        const long n = std::strtol(v, nullptr, 10);
        if (n >= 1 && n <= 512) return static_cast<std::size_t>(n);
    }
    return 24;
}

std::string trimmed(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

} // namespace

void init(nettls::Config cfg, const std::string& plain_origin) {
    g_cfg = std::move(cfg);
    g_plain_origin = trimmed(plain_origin);
}

bool enabled_for(const std::string& base_url) {
    return g_cfg.enabled() && trimmed(base_url) != g_plain_origin;
}

nettls::ClientPool* pool_for(const std::string& base_url, int read_timeout_sec) {
    if (!enabled_for(base_url)) return nullptr;
    const std::string key = trimmed(base_url) + "|" + std::to_string(read_timeout_sec);
    std::lock_guard<std::mutex> lock(g_pools_mu);
    auto it = g_pools.find(key);
    if (it == g_pools.end()) {
        // Ждать свободного клиента не дольше, чем читать ответ (как при обычном HTTP без пула).
        it = g_pools
                 .emplace(key, std::make_unique<nettls::ClientPool>(trimmed(base_url), g_cfg, 10, read_timeout_sec,
                                                                    pool_limit(), pool_limit(), read_timeout_sec))
                 .first;
    }
    return it->second.get();
}

httplib::Client client(const std::string& base_url) {
    return nettls::client_value(base_url, enabled_for(base_url) ? g_cfg : nettls::Config{});
}

std::unique_ptr<httplib::Client> make(const std::string& base_url) {
    return nettls::make_client(base_url, enabled_for(base_url) ? g_cfg : nettls::Config{});
}

} // namespace upstream_tls
