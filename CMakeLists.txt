cmake_minimum_required(VERSION 3.14)
project(gxnet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GXNET_BUILD_TESTS "Build the test suite" ON)
option(GXNET_BUILD_EXAMPLES "Build the gxlint tool" ON)
option(GXNET_BUILD_LINK "Build the transport layer (mock everywhere, BCS on Windows)" ON)
# Off by default: the application fetches and builds wxWidgets through CPM, and
# the library itself must stay buildable in seconds with no network.
option(GXNET_BUILD_APP "Build the gxdemo application (fetches wxWidgets)" OFF)

# Sanitizers, off by default and worth turning on for a test run.
#
# Semicolon-separated, passed straight to -fsanitize: "address;undefined" is the
# usual pair, "thread" has to be built on its own because it cannot be combined
# with address. MSVC accepts "address" alone: it has no UndefinedBehaviorSanitizer
# and no ThreadSanitizer, so anything else is refused rather than silently doing
# nothing.
# NOMINMAX for every Windows target, on the command line rather than in a
# header. windows.h defines min and max as macros, any header can pull it in
# first, and the resulting error names a line of ours that is perfectly correct.
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

set(GXNET_SANITIZE "" CACHE STRING "Sanitizers to build with, e.g. address;undefined")

if(GXNET_SANITIZE)
    if(MSVC)
        # MSVC has AddressSanitizer and nothing else: no UBSan, no TSan. Its
        # flag is also spelled differently, and it must be added rather than
        # replacing CMAKE_CXX_FLAGS, or /EHsc goes with it and every throw
        # becomes a warning and then a crash.
        if(NOT GXNET_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only GXNET_SANITIZE=address")
        endif()
        add_compile_options(/fsanitize=address /Zi)
    else()
        string(REPLACE ";" "," _gxnet_sanitize "${GXNET_SANITIZE}")
        # -fno-omit-frame-pointer for readable stacks, -g so they carry line
        # numbers even in a release build, and the flags go to the linker too:
        # a sanitized object linked without them fails at run time, not at link.
        add_compile_options(-fsanitize=${_gxnet_sanitize} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${_gxnet_sanitize})
    endif()
    message(STATUS "gxnet: sanitizers ${GXNET_SANITIZE}")
endif()

# One directory per component, each with its own include/ and src/. The library
# used to sit at the top of the tree while everything else was a subdirectory,
# which stopped reading well as soon as there was more than one consumer.
add_subdirectory(core)

if(GXNET_BUILD_LINK)
    add_subdirectory(link)
endif()

if(GXNET_BUILD_TESTS)
    enable_testing()
    add_executable(gxnet_tests tests/test_gxnet.cpp)
    target_link_libraries(gxnet_tests PRIVATE gxnet)
    add_test(NAME gxnet_tests COMMAND gxnet_tests)

    if(GXNET_BUILD_LINK)
        add_executable(gxnet_link_tests tests/test_link.cpp)
        target_link_libraries(gxnet_link_tests PRIVATE gxnet_link)
        add_test(NAME gxnet_link_tests COMMAND gxnet_link_tests)

        # gxdemo's own logic, tested without a window server.
        #
        # Session is the application's whole model -- the connection, the log,
        # the spontaneous listeners, the firmware gate -- and it deliberately
        # includes no wxWidgets header. That is what makes this target possible
        # at all: it compiles one file out of app/ against the mock transport
        # and runs anywhere, including in a build that never fetched wx.
        #
        # The panels are not covered here. They need a running wxApp, and that
        # is a separate target with a separate set of problems.
        add_executable(gxdemo_tests tests/test_gxdemo.cpp app/src/session.cpp)
        target_include_directories(gxdemo_tests PRIVATE app/src)
        target_link_libraries(gxdemo_tests PRIVATE gxnet_link)
        add_test(NAME gxdemo_tests COMMAND gxdemo_tests)
    endif()
endif()

if(GXNET_BUILD_EXAMPLES)
    add_executable(gxlint examples/gxlint.cpp)
    target_link_libraries(gxlint PRIVATE gxnet)
endif()

if(GXNET_BUILD_APP)
    add_subdirectory(app)
endif()
