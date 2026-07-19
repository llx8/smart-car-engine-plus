cmake_minimum_required(VERSION 3.16)
project(smart_car_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

cmake_policy(SET CMP0135 NEW)

# 模块开关 — 开发哪个开哪个，增量构建
option(BUILD_CAR_CORE     "Build car_core main process"        OFF)
option(BUILD_ACTUATOR_SRV "Build actuator_srv service"         OFF)
option(BUILD_TOOLS        "Build tools (can_sim / car_ctl)"    ON)
option(BUILD_DASHBOARD    "Build Qt5 dashboard"                OFF)
option(BUILD_TESTS        "Build unit tests"                   ON)

# spdlog (common/Logger)
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found, fetching...")
    FetchContent_Declare(spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.12.0.zip
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# nlohmann/json (car_ai) — car_ai 暂未开发，放条件块备用
if(BUILD_TOOLS)
    find_package(nlohmann_json QUIET)
    if(NOT nlohmann_json_FOUND)
        message(STATUS "nlohmann_json not found, fetching for car_ai...")
        FetchContent_Declare(nlohmann_json
            URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
        )
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endif()

add_subdirectory(common)

if(BUILD_CAR_CORE)
    add_subdirectory(car_core)
endif()

if(BUILD_ACTUATOR_SRV)
    add_subdirectory(actuator_srv)
endif()

if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(BUILD_DASHBOARD)
    find_package(Qt5 QUIET COMPONENTS Widgets Core)
    if(Qt5_FOUND)
        add_subdirectory(car_dashboard)
    else()
        message(STATUS "Qt5 not found — skipping car_dashboard")
    endif()
endif()

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
