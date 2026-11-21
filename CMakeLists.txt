cmake_minimum_required(VERSION 3.28)
project(disktree VERSION 1.1.0 LANGUAGES CXX)

# Static MSVC runtime only for the static triplet (single shippable .exe).
# The dev preset uses vcpkg's dynamic x64-windows triplet, whose libraries
# are built against the DLL runtime, so it must keep /MD.
if(MSVC)
    if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
    # Sources contain UTF-8 box-drawing characters
    add_compile_options(/utf-8 /permissive-)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX)   # no min/max macros from windows.h
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DISKTREE_BUILD_TESTS      "Build unit tests"        ON)
option(DISKTREE_BUILD_BENCHMARKS "Build the scan benchmark" ON)

# ── Find dependencies ────────────────────────────────────────────
find_package(CLI11         CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt           CONFIG REQUIRED)
find_package(ftxui         CONFIG REQUIRED)
find_package(zstd          CONFIG REQUIRED)
find_package(httplib       CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# ── Embedded web assets (so the web UI works offline) ────────────
set(DISKTREE_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
add_custom_command(
    OUTPUT  ${DISKTREE_GEN_DIR}/disktree/d3_js.hpp
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_SOURCE_DIR}/third_party/d3/d3.min.js
            -DOUTPUT=${DISKTREE_GEN_DIR}/disktree/d3_js.hpp
            -DSYMBOL=d3_min_js
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/third_party/d3/d3.min.js
            ${CMAKE_SOURCE_DIR}/cmake/embed_file.cmake
    COMMENT "Embedding d3.min.js"
)

# ── Core library ────────────────────────────────────────────────
add_library(disktree_core STATIC
    src/utils/humanize.cpp
    src/utils/progress.cpp
    src/utils/thread_pool.cpp
    src/core/arena.cpp
    src/core/scanner.cpp
    src/core/builder.cpp
    src/core/index.cpp
    src/core/snapshot.cpp
    src/core/snapshot_store.cpp
    src/platform/drives.cpp
    src/service/engine.cpp
    src/service/api_server.cpp
    src/export/json_export.cpp
    src/export/csv_export.cpp
    src/interfaces/tui/app.cpp
    src/interfaces/web/server.cpp
    ${DISKTREE_GEN_DIR}/disktree/d3_js.hpp
)

target_include_directories(disktree_core
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${DISKTREE_GEN_DIR}
)

target_compile_definitions(disktree_core
    PUBLIC DISKTREE_VERSION="${PROJECT_VERSION}"
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
        unofficial::sqlite3::sqlite3
        $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,
            zstd::libzstd_shared,
            zstd::libzstd_static>
)

if(WIN32)
    target_link_libraries(disktree_core PUBLIC shell32 ole32)
endif()

# ── Executable ──────────────────────────────────────────────────
add_executable(disktree src/main.cpp)
target_link_libraries(disktree PRIVATE disktree_core)

# ── Tests ───────────────────────────────────────────────────────
if(DISKTREE_BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Benchmarks ──────────────────────────────────────────────────
if(DISKTREE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
