// Гейтвей → апстримы по mTLS (change add-internal-mtls, шаг 7, design D6 и D9).
//
// Включается INTERNAL_MTLS=true (весь контур). Тогда вызовы к api, report, ai, mcp, web и docs идут
// по https с клиентским сертификатом, а на горячем пути — через пул постоянных соединений
// (рукопожатие не на каждый запрос). Исключение — google-auth: его публичный порт (/auth/*, JWKS)
// остаётся обычным HTTP, как и входной порт самого гейтвея (D9). Выключено — всё как раньше.
#pragma once

#include <memory>
#include <string>

#include "httplib.h"
#include "net/pool.hpp"
#include "net/tls.hpp"

namespace upstream_tls {

// Один раз при старте, до запуска сервера. cfg пуст — mTLS выключен. plain_origin — апстрим,
// который всегда остаётся на HTTP (публичный порт google-auth).
void init(nettls::Config cfg, const std::string& plain_origin);

// К этому апстриму ходят по mTLS?
bool enabled_for(const std::string& base_url);

// Пул постоянных клиентов к апстриму (на каждую пару апстрим + таймаут чтения свой). nullptr —
// апстрим без mTLS, вызывающий работает как раньше.
nettls::ClientPool* pool_for(const std::string& base_url, int read_timeout_sec);

// Клиент по значению (редкие вызовы: /version, openapi); бросает std::runtime_error, если TLS
// включён, а клиент создать нельзя.
httplib::Client client(const std::string& base_url);

// Отдельный клиент мимо пула (SSE-потоки чата). nullptr — клиент создать нельзя.
std::unique_ptr<httplib::Client> make(const std::string& base_url);

} // namespace upstream_tls
