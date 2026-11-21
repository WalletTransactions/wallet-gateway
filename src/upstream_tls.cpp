#include "upstream_tls.hpp"

#include <map>
#include <mutex>

namespace upstream_tls {

namespace {

nettls::Config g_cfg;
std::string g_plain_origin;
std::mutex g_pools_mu;
std::map<std::string, std::unique_ptr<nettls::ClientPool>> g_pools;

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
        it = g_pools.emplace(key, std::make_unique<nettls::ClientPool>(trimmed(base_url), g_cfg, 10, read_timeout_sec)).first;
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
