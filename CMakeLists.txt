cmake_minimum_required(VERSION 3.16)
project(talon-async-io VERSION 2.1.0 LANGUAGES CXX)

# ============================================================================
# C++ Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Options
# ============================================================================
option(TALON_BUILD_TESTS "Build test suite" ON)
option(TALON_BUILD_EXAMPLES "Build example programs" ON)
option(TALON_ENABLE_DEBUG "Enable TALON_DEBUG compile definition" OFF)

# ============================================================================
# Dependencies
# ============================================================================
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBURING REQUIRED liburing)
else()
    find_library(LIBURING_LIBRARIES NAMES uring)
    find_path(LIBURING_INCLUDE_DIRS NAMES liburing.h)
    if(NOT LIBURING_LIBRARIES)
        message(FATAL_ERROR "liburing not found. Install liburing-dev.")
    endif()
endif()

# ============================================================================
# Header-Only Library Target
# ============================================================================
add_library(talon_async_io INTERFACE)
target_include_directories(talon_async_io INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(talon_async_io INTERFACE
    Threads::Threads
    ${LIBURING_LIBRARIES}
)
if(LIBURING_INCLUDE_DIRS)
    target_include_directories(talon_async_io INTERFACE ${LIBURING_INCLUDE_DIRS})
endif()

if(TALON_ENABLE_DEBUG)
    target_compile_definitions(talon_async_io INTERFACE TALON_DEBUG)
endif()

# Compiler warnings for the library (apply to consumers via INTERFACE).
# -Wno-c++20-extensions is Clang-specific.
# -Wno-format-security is needed because DebugLog takes a runtime format
#   string (template parameter pack) that the compiler cannot verify;
#   all call sites use string literals, so this is safe.
set(TALON_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wno-format-security)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    list(APPEND TALON_WARNING_FLAGS -Wno-c++20-extensions)
endif()
target_compile_options(talon_async_io INTERFACE ${TALON_WARNING_FLAGS})

# ============================================================================
# Installation
# ============================================================================
include(GNUInstallDirs)
install(DIRECTORY include/talon DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS talon_async_io EXPORT talon_async_io-config)

# ============================================================================
# Subdirectories
# ============================================================================
if(TALON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TALON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
