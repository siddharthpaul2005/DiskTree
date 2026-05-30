cmake_minimum_required(VERSION 3.28)
project(disktree VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Find dependencies ────────────────────────────────────────────
find_package(CLI11         CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt           CONFIG REQUIRED)
find_package(ftxui         CONFIG REQUIRED)
find_package(zstd          CONFIG REQUIRED)
find_package(Catch2        CONFIG REQUIRED)
find_package(httplib       CONFIG REQUIRED)

# ── Core library ────────────────────────────────────────────────
add_library(disktree_core STATIC
    src/utils/humanize.cpp
    src/utils/thread_pool.cpp
    src/core/arena.cpp
    src/core/scanner.cpp
    src/core/builder.cpp
    src/core/index.cpp
    src/export/json_export.cpp
    src/export/csv_export.cpp
)

target_include_directories(disktree_core
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(disktree_core
    PUBLIC
        CLI11::CLI11
        nlohmann_json::nlohmann_json
        fmt::fmt
        ftxui::screen
        ftxui::dom
        ftxui::component
        httplib::httplib
        $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,
            zstd::libzstd_shared,
            zstd::libzstd_static>
)

# ── Executable ──────────────────────────────────────────────────
add_executable(disktree src/main.cpp)
target_link_libraries(disktree PRIVATE disktree_core)

# ── Tests ───────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)