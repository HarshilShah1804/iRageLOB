cmake_minimum_required(VERSION 3.16)
project(binance_capture LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -O2 -Werror)
endif()

if(DEFINED ENV{CONDA_PREFIX})
    set(CONDA_ENV_PREFIX "$ENV{CONDA_PREFIX}" CACHE PATH "Conda environment prefix")
else()
    set(CONDA_ENV_PREFIX "$ENV{HOME}/miniconda3/envs/irage" CACHE PATH "Conda environment prefix")
endif()

message(STATUS "Conda env prefix: ${CONDA_ENV_PREFIX}")

list(PREPEND CMAKE_PREFIX_PATH "${CONDA_ENV_PREFIX}")
list(PREPEND CMAKE_INCLUDE_PATH "${CONDA_ENV_PREFIX}/include")
list(PREPEND CMAKE_LIBRARY_PATH "${CONDA_ENV_PREFIX}/lib")

find_package(Boost 1.80 REQUIRED COMPONENTS system)

find_package(OpenSSL REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/rapidjson/include")
    include_directories(${CMAKE_SOURCE_DIR}/third_party/rapidjson/include)
    message(STATUS "Using vendored RapidJSON")
else()
    find_path(RAPIDJSON_INCLUDE_DIR rapidjson/document.h
        HINTS ${CONDA_ENV_PREFIX}/include /usr/include /usr/local/include)
    if(RAPIDJSON_INCLUDE_DIR)
        include_directories(${RAPIDJSON_INCLUDE_DIR})
        message(STATUS "Using system RapidJSON: ${RAPIDJSON_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "RapidJSON not found. Please run: git clone --depth 1 https://github.com/Tencent/rapidjson.git third_party/rapidjson")
    endif()
endif()


add_executable(binance_capture
    src/main.cpp
    src/binance_ws_client.cpp
    src/event_parser.cpp
    src/replay_engine.cpp
)

target_include_directories(binance_capture PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CONDA_ENV_PREFIX}/include      # Boost, OpenSSL headers
)

target_link_directories(binance_capture PRIVATE
    ${CONDA_ENV_PREFIX}/lib
)

target_link_libraries(binance_capture PRIVATE
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

enable_testing()
add_subdirectory(tests)
