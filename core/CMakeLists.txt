# The library the repository is named after: representation, encoding, parsing
# and validation of GxNet telegrams.
#
# No dependencies, and no transport. Everything that talks to a device lives in
# link/; everything a person clicks lives in app/.

add_library(gxnet
    src/core.cpp
    src/escape.cpp
    src/telegram.cpp
    src/binary.cpp
    src/codec.cpp
    src/validate.cpp
    src/gxnet_c.cpp
)

target_include_directories(gxnet
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    # /utf-8 sets both the source and the execution character set. Without it
    # MSVC reads sources as the system code page and narrows string literals to
    # it, which on a Russian Windows quietly mangles any non-ASCII text on its
    # way to a label. Everything here is UTF-8 end to end.
    target_compile_options(gxnet PRIVATE /W4 /utf-8)
else()
    target_compile_options(gxnet PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Included here rather than relying on the top level: this directory is added
# before the top-level include(GNUInstallDirs) runs.
include(GNUInstallDirs)
install(TARGETS gxnet ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
