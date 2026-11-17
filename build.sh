#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")"

# 1. Копируем клиент в серверную статику
rm -rf server/web/static
mkdir -p server/web/static
cp -r client/* server/web/static/

# 2. Собираем под разные платформы
# Windows x64 (основная для школьных ПК)
CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -o dist/mcg-server-windows-amd64.exe ./server
# Windows 386 (для очень старых ПК, но не XP — см. примечание)
CGO_ENABLED=0 GOOS=windows GOARCH=386 go build -o dist/mcg-server-windows-386.exe ./server
# Linux amd64
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -o dist/mcg-server-linux-amd64 ./server
# Android arm64 (Termux)
CGO_ENABLED=0 GOOS=android GOARCH=arm64 go build -o dist/mcg-server-android-arm64 ./server
# macOS (Intel + Apple Silicon)
CGO_ENABLED=0 GOOS=darwin GOARCH=amd64 go build -o dist/mcg-server-darwin-amd64 ./server
CGO_ENABLED=0 GOOS=darwin GOARCH=arm64 go build -o dist/mcg-server-darwin-arm64 ./server

echo "Готово. Бинарники в dist/"