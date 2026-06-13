#!/bin/bash
set -e

PROJ="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJ/build"

echo "==> Creating runtime directories..."
sudo mkdir -p /var/oj/sessions /tmp/oj_sandbox
sudo chown -R "$(whoami)" /var/oj/sessions /tmp/oj_sandbox 2>/dev/null || true

echo "==> Building..."
cmake -S "$PROJ" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --target oj_backend -j$(nproc)

echo "==> Starting oj_backend..."
cd "$PROJ"
exec "$BUILD_DIR/oj_backend" "$PROJ/config/config.json"
