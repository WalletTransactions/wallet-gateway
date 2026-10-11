cmake_minimum_required(VERSION 3.16)
project(wallet_gateway CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Общий код сервисов (util/: jwt, jwks, crypto, log, metrics, ...) — копия
# репозитория wallet-common, обновляется его scripts/vendor.sh; руками не править.
set(WALLET_COMMON_DIR ${CMAKE_SOURCE_DIR}/third_party/wallet-common)

# OpenSSL — только для RSA-SHA256-verify (проверка подписи access-токена на
# защищённых префиксах по публичному ключу с JWKS auth-сервиса, см.
# src/util/crypto.cpp — никакого приватного ключа/секрета тут нет).
# Апстримы сейчас все на localhost по обычному http — если один из них
# станет https:// в проде, добавьте CPPHTTPLIB_OPENSSL_SUPPORT (как в
# CMakeLists.txt google-auth).
#
# ZLIB — gzip браузеру (по прямому запросу — "правь все пункты по нагрузке"):
# апстримам (wallet-api/wallet-report) гейтвей Accept-Encoding не форвардит
# вовсе (см. src/main.cpp::is_accept_encoding, почему) — сжимает для браузера
# он сам, автоматически, тем же встроенным механизмом cpp-httplib, что и у
# апстримов (см. их CMakeLists.txt — тот же комментарий целиком).
# CPPHTTPLIB_ZLIB_SUPPORT здесь включает и Client, и Server одним макросом
# (один заголовок на весь бинарник) — Client'у эта возможность не нужна и не
# используется (апстрим никогда не пришлёт Content-Encoding, раз Accept-Encoding
# ему не ушёл), но именно поэтому "лишнее" безопасно: код декомпрессии просто
# ни разу не активируется.
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Версия приложения — проставляется на билде, не хардкодится в исходниках
# (см. GET /version, src/docs.cpp, и build_info в GET /metrics, src/util/
# metrics.cpp). Единственный источник самого номера — файл VERSION рядом с
# этим CMakeLists.txt (semver, правится руками при релизе); он же копируется
# в Containerfile (COPY VERSION ./), поэтому одинаково читается что при
# нативной сборке (cmake -B build), что при сборке образа.
#
# Git-хэш — ДОПОЛНИТЕЛЬНО к VERSION, не вместо: отличает два билда с одним и
# тем же номером версии между релизами. У сборки ВНУТРИ контейнера своего
# .git нет (Containerfile копирует только исходники, не весь репозиторий) —
# поэтому сначала смотрим на кэш-переменную GIT_COMMIT: её передаёт
# Containerfile (-DGIT_COMMIT=${GIT_COMMIT}, а туда — ARG GIT_COMMIT,
# который в свою очередь снаружи передаёт `podman build --build-arg
# GIT_COMMIT=$(git rev-parse --short HEAD)`, см. compose.yaml). Нативная
# сборка на рабочей машине эту переменную не задаёт — тогда спрашиваем git
# напрямую, репозиторий уже на месте. Не нашли ни то, ни другое — не падаем,
# версия из VERSION всё равно есть, просто без хэша ("unknown").
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION APP_VERSION_LINES LIMIT_COUNT 1)
list(GET APP_VERSION_LINES 0 APP_VERSION)
string(STRIP "${APP_VERSION}" APP_VERSION)

if(NOT DEFINED GIT_COMMIT OR "${GIT_COMMIT}" STREQUAL "")
    find_program(GIT_EXECUTABLE git)
    set(GIT_COMMIT "unknown")
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --abbrev=8
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE GIT_DESCRIBE_RESULT
            ERROR_QUIET
        )
        if(NOT GIT_DESCRIBE_RESULT EQUAL 0 OR "${GIT_COMMIT}" STREQUAL "")
            set(GIT_COMMIT "unknown")
        endif()
    endif()
endif()
message(STATUS "wallet_gateway version: ${APP_VERSION} (${GIT_COMMIT})")

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/version.hpp
    @ONLY
)

add_executable(wallet_gateway
    src/main.cpp
    src/docs.cpp
    ${WALLET_COMMON_DIR}/util/base64.cpp
    ${WALLET_COMMON_DIR}/util/crypto.cpp
    ${WALLET_COMMON_DIR}/util/jwt.cpp
    ${WALLET_COMMON_DIR}/util/jwt_claims.cpp
    ${WALLET_COMMON_DIR}/util/jwks.cpp
    ${WALLET_COMMON_DIR}/util/dotenv.cpp
    ${WALLET_COMMON_DIR}/util/log.cpp
    ${WALLET_COMMON_DIR}/util/metrics.cpp
)

target_include_directories(wallet_gateway PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${WALLET_COMMON_DIR}
    ${CMAKE_BINARY_DIR}/generated
)

target_compile_definitions(wallet_gateway PRIVATE
    CPPHTTPLIB_ZLIB_SUPPORT
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
target_compile_options(wallet_gateway PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(wallet_gateway PRIVATE
    OpenSSL::Crypto
    Threads::Threads
    ZLIB::ZLIB
)

if(WIN32)
    target_link_libraries(wallet_gateway PRIVATE ws2_32 crypt32)

    add_custom_command(TARGET wallet_gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/scripts/windows
                $<TARGET_FILE_DIR:wallet_gateway>
        COMMENT "Копирую scripts/windows/*.bat рядом с wallet_gateway.exe"
    )
else()
    add_custom_command(TARGET wallet_gateway POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/scripts/linux
                $<TARGET_FILE_DIR:wallet_gateway>
        COMMENT "Копирую scripts/linux/*.sh рядом с wallet_gateway"
    )
endif()

# openapi.yaml + вендоренный Swagger UI — рядом с исполняемым файлом, та же
# рабочая директория, что у .env (см. src/docs.cpp — читает их с диска по
# относительному пути при каждом GET /openapi.yaml, /docs). На обеих
# платформах — своя спека гейтвея (маршрутизация + грубый JWT-гейт), не
# путать с /openapi.yaml апстримов (wallet-api/google-auth), она недоступна
# через гейтвей (find_route отправит запрос дальше, см. main.cpp).
add_custom_command(TARGET wallet_gateway POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/openapi.yaml
            $<TARGET_FILE_DIR:wallet_gateway>/openapi.yaml
    COMMENT "Копирую openapi.yaml рядом с wallet_gateway (GET /openapi.yaml, /docs)"
)
add_custom_command(TARGET wallet_gateway POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/swagger-ui
            $<TARGET_FILE_DIR:wallet_gateway>/vendor/swagger-ui
    COMMENT "Копирую вендоренный Swagger UI рядом с wallet_gateway (GET /docs)"
)
