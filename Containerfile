# syntax=docker/dockerfile:1
#
# Multi-stage build wallet-gateway. Как и wallet-api — нужен OpenSSL для
# проверки подписи JWT по JWKS апстрима; в отличие от wallet-api — без БД
# (гейтвей только маршрутизирует, никаких данных не хранит). curl в рантайме
# — только для healthcheck'а в compose.yaml (GET /healthz), сам образ его
# не содержит.

FROM debian:bookworm-slim AS build

# GIT_COMMIT — короткий хэш коммита (git rev-parse --short HEAD/git describe
# на хосте, где ЕСТЬ .git — у этой стадии своего нет, .git в контекст сборки
# не входит), передаётся снаружи через `podman build --build-arg
# GIT_COMMIT=...` (см. compose.yaml). Не передали — CMakeLists.txt сам
# попробует спросить git и, если не найдёт, поставит "unknown" (версия из
# VERSION при этом всё равно на месте, см. CMakeLists.txt, "Версия
# приложения").
ARG GIT_COMMIT=unknown
# APP_BUILD — тег образа (`0.1.1-rc.2`), его передаёт release.yml; пусто —
# CMake подставит `<VERSION>-dev` (GET /version, поле build).
ARG APP_BUILD=

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libssl-dev \
        zlib1g-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY VERSION ./
COPY openapi.yaml ./
COPY src/ src/
COPY third_party/ third_party/
COPY scripts/ scripts/
# ^ нужен: CMakeLists.txt в Linux-ветке копирует scripts/linux/*.sh рядом с
#   бинарником через POST_BUILD add_custom_command (copy_directory), без
#   EXISTS-проверки — без этой папки в контексте сборка упадёт. openapi.yaml
#   рядом по той же причине — тот же POST_BUILD копирует и её
#   (copy_if_different ${CMAKE_SOURCE_DIR}/openapi.yaml, см. CMakeLists.txt) —
#   без этой строки сборка стадии build падала на "no such file or directory"
#   ("Error copying file (if different)"), хотя runtime-стадия ниже эту же
#   openapi.yaml берёт напрямую из контекста, а не из стадии build.

ARG BUILD_JOBS=3
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DGIT_COMMIT="${GIT_COMMIT}" -DAPP_BUILD="${APP_BUILD}" \
    && cmake --build build --config Release -j"${BUILD_JOBS}"

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libssl3 \
        zlib1g \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /app --shell /usr/sbin/nologin appuser

WORKDIR /app
COPY --from=build /src/build/wallet_gateway /app/wallet_gateway
# openapi.yaml + вендоренный Swagger UI (гейтвейной СВОЕЙ спеки, не
# апстримов) — читаются с диска при каждом GET /openapi.yaml, /docs (см.
# src/docs.cpp) — чистая статика, копируется прямо из контекста сборки.
COPY openapi.yaml /app/openapi.yaml
COPY third_party/swagger-ui/ /app/vendor/swagger-ui/

USER appuser
EXPOSE 8090

ENTRYPOINT ["/app/wallet_gateway"]
