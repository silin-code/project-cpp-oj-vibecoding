cmake_minimum_required(VERSION 3.16)
project(oj_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Dependencies ──────────────────────────────────────

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")
include_directories(${THIRD_PARTY_DIR})

find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

# MySQL client library
pkg_check_modules(MYSQL REQUIRED mysqlclient)
include_directories(${MYSQL_INCLUDE_DIRS})
link_directories(${MYSQL_LIBRARY_DIRS})

# libseccomp
pkg_check_modules(SECCOMP REQUIRED libseccomp)
include_directories(${SECCOMP_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR}/src)

# ── Source files ──────────────────────────────────────

file(GLOB_RECURSE SOURCES "src/*.cc")

# ── Executable ────────────────────────────────────────

add_executable(oj_backend ${SOURCES})

target_link_libraries(oj_backend
    PRIVATE
    ${MYSQL_LIBRARIES}
    ${SECCOMP_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    dl
    crypt
)

# Copy public/ to build directory for development
add_custom_command(TARGET oj_backend POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/public"
        "$<TARGET_FILE_DIR:oj_backend>/public"
    COMMENT "Copying static files..."
)

# ── Tests ─────────────────────────────────────────────
#
# 独立模块测试在 test/<module>_test/build.sh，只编译该模块需要的源文件，
# 避免全量编译。CMake 只保留 oj_backend 主目标。
#
