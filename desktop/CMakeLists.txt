cmake_minimum_required(VERSION 3.29)

project(SecureBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

set(Qt6_DIR "" CACHE PATH "Directory containing Qt6Config.cmake for the approved Qt kit")

if(NOT Qt6_DIR OR NOT EXISTS "${Qt6_DIR}/Qt6Config.cmake")
    message(FATAL_ERROR
        "Qt 6.12.0 MSVC 2022 x64 is required. "
        "Configure with -DQt6_DIR=<Qt-kit>/lib/cmake/Qt6, "
        "pointing to the directory containing Qt6Config.cmake."
    )
endif()

find_package(Qt6 6.12.0 EXACT REQUIRED CONFIG COMPONENTS Widgets NO_DEFAULT_PATH)

add_executable(sbp_toolchain_check main.cpp)
target_link_libraries(sbp_toolchain_check PRIVATE Qt6::Widgets)
