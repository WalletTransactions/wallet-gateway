#!/usr/bin/env bash
# Останавливает и удаляет systemd-сервис wallet-gateway.
# /opt/wallet-gateway (бинарник, .env) не трогает — удалите вручную при необходимости.
set -euo pipefail

SERVICE_NAME="wallet-gateway"

if [ "$(id -u)" -ne 0 ]; then
    echo "Нужны права root. Запустите: sudo $0" >&2
    exit 1
fi

systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
systemctl disable "${SERVICE_NAME}" 2>/dev/null || true
rm -f "/etc/systemd/system/${SERVICE_NAME}.service"
systemctl daemon-reload

echo "Служба ${SERVICE_NAME} удалена. /opt/wallet-gateway оставлен как есть."
