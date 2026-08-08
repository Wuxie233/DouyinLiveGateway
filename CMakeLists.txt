cmake_minimum_required(VERSION 3.20)
project(DouyinLiveGateway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(douyin_gateway_core
    src/Arguments.cpp
    src/Json.cpp
    src/LiveDataMapper.cpp
    src/LogBridge.cpp)
target_include_directories(douyin_gateway_core PUBLIC include)
if(MSVC)
    target_compile_options(douyin_gateway_core PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET douyin_gateway_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(douyin_gateway_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(DouyinLiveGateway.CoreTests tests/CoreTests.cpp)
    target_link_libraries(DouyinLiveGateway.CoreTests PRIVATE douyin_gateway_core)
    add_test(NAME DouyinLiveGateway.CoreTests COMMAND DouyinLiveGateway.CoreTests)
endif()

if(WIN32)
    add_executable(DouyinLiveGateway src/GatewayMain.cpp)
    target_link_libraries(DouyinLiveGateway PRIVATE douyin_gateway_core)
    target_compile_definitions(DouyinLiveGateway PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
