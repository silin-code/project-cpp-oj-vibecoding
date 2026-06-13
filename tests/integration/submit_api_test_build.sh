#!/bin/bash
set -e
PROJ="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="/tmp/oj_test_build"
mkdir -p "$BUILD_DIR"

CXXFLAGS="-std=c++17 -I$PROJ/src -I$PROJ/third_party $(pkg-config --cflags mysqlclient 2>/dev/null)"
LDFLAGS="$(pkg-config --libs mysqlclient 2>/dev/null) -lssl -lcrypto -lpthread -lcrypt -ldl -lseccomp -lgtest -lgtest_main"

echo "==> Compiling submit_api_test..."
g++ $CXXFLAGS -c "$PROJ/src/utils/logger.cc"            -o "$BUILD_DIR/logger.o"
g++ $CXXFLAGS -c "$PROJ/src/utils/config.cc"             -o "$BUILD_DIR/config.o"
g++ $CXXFLAGS -c "$PROJ/src/db/connection_pool.cc"       -o "$BUILD_DIR/connection_pool.o"
g++ $CXXFLAGS -c "$PROJ/src/service/auth_service.cc"     -o "$BUILD_DIR/auth_service.o"
g++ $CXXFLAGS -c "$PROJ/src/handler/auth_handler.cc"     -o "$BUILD_DIR/auth_handler.o"
g++ $CXXFLAGS -c "$PROJ/src/service/problem_service.cc"  -o "$BUILD_DIR/problem_service.o"
g++ $CXXFLAGS -c "$PROJ/src/service/executor_service.cc" -o "$BUILD_DIR/executor_service.o"
g++ $CXXFLAGS -c "$PROJ/src/handler/submit_handler.cc"   -o "$BUILD_DIR/submit_handler.o"
g++ $CXXFLAGS -c "submit_api_test.cc"                    -o "$BUILD_DIR/submit_api_test.o"

echo "==> Linking..."
g++ "$BUILD_DIR"/{logger,config,connection_pool,auth_service,auth_handler,problem_service,executor_service,submit_handler,submit_api_test}.o $LDFLAGS -o "$BUILD_DIR/submit_api_test"

echo "==> Running (working dir: $PROJ)..."
cd "$PROJ"
"$BUILD_DIR/submit_api_test" "$@"
