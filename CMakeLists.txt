cmake_minimum_required(VERSION 3.24)

project(dell-toolbox
    VERSION 0.1.0
    DESCRIPTION "Open-source Dell toolbox: thermal control, power management"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON) # qrc files listed as sources get compiled (qt_add_resources silently no-ops here)

option(DTB_HAL "Hardware backend: 'win' (real) or 'mock' (dev/tests)" "mock")
option(DTB_REQUIRE_ADMIN "Embed the requireAdministrator manifest" ON)
if(NOT DTB_HAL MATCHES "^(win|mock)$")
    message(FATAL_ERROR "DTB_HAL must be 'win' or 'mock', got '${DTB_HAL}'")
endif()
if(DTB_HAL STREQUAL "win" AND NOT WIN32)
    message(FATAL_ERROR "DTB_HAL=win requires a Windows toolchain (CI windows-latest)")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)
qt_standard_project_setup()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING AND DTB_HAL STREQUAL "mock" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()
