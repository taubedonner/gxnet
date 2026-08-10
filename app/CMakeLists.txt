# gxdemo -- the operator-facing side of the library.
#
# Native widgets, drawn by the platform. No OpenGL, no GPU driver in the path:
# on the machine this has to run on, the driver would not render a window at
# all, and a labelling line is no place to be debugging graphics stacks.
#
# Kept out of the default build (GXNET_BUILD_APP is OFF) because wxWidgets is
# fetched and built from source. The library itself must stay buildable in
# seconds with no network.

# Several dependencies still declare cmake_minimum_required below 3.5, which
# CMake 4 refuses outright.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)

set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)
set(wxBUILD_MONOLITHIC OFF CACHE BOOL "" FORCE)
# Nothing here needs a browser, media playback or the OpenGL canvas; leaving
# them out keeps both the build and the binary smaller.
set(wxUSE_WEBVIEW OFF CACHE BOOL "" FORCE)
set(wxUSE_MEDIACTRL OFF CACHE BOOL "" FORCE)
set(wxUSE_OPENGL OFF CACHE BOOL "" FORCE)
set(wxUSE_RIBBON OFF CACHE BOOL "" FORCE)
set(wxUSE_PROPGRID OFF CACHE BOOL "" FORCE)
set(wxUSE_STC OFF CACHE BOOL "" FORCE)

# No image codecs: this program shows text, numbers and lists, and wx's bundled
# libpng does not even compile against a current macOS SDK (it still reaches for
# the Classic Mac OS fp.h). Dropping them removes the problem and shrinks both
# the build and the binary.
set(wxUSE_LIBPNG OFF CACHE STRING "" FORCE)
set(wxUSE_LIBJPEG OFF CACHE STRING "" FORCE)
set(wxUSE_LIBTIFF OFF CACHE STRING "" FORCE)
set(wxUSE_LIBLZMA OFF CACHE STRING "" FORCE)
# Both of these are built on PNG and refuse to configure without it. The stock
# platform art provider supplies the few standard icons the dialogs use.
set(wxUSE_ARTPROVIDER_TANGO OFF CACHE BOOL "" FORCE)
set(wxUSE_SVG OFF CACHE BOOL "" FORCE)

# 3.3.x rather than 3.2.x: the older branch does not build against a current
# macOS SDK (it calls CGDisplayCreateImage, removed in macOS 15) and carries a
# vendored libpng that reaches for the Classic Mac OS fp.h.
CPMAddPackage(
    NAME wxWidgets
    GITHUB_REPOSITORY wxWidgets/wxWidgets
    GIT_TAG v3.3.3.1
    GIT_SUBMODULES_RECURSE ON
    OPTIONS
        "wxBUILD_SHARED OFF"
        "wxBUILD_TESTS OFF"
        "wxBUILD_SAMPLES OFF"
        "wxBUILD_DEMOS OFF"
        "wxBUILD_PRECOMP ON"
)

add_executable(gxdemo WIN32
    src/main.cpp
    src/session.cpp
    src/mainframe.cpp
    src/format.cpp
    src/panels/connection.cpp
    src/panels/device.cpp
    src/panels/unique.cpp
    src/panels/plu.cpp
    src/panels/buffer.cpp
    src/panels/terminal.cpp
    src/panels/dialog.cpp
    src/panels/softkey.cpp
    src/panels/ftp.cpp
    src/panels/console.cpp
    src/panels/reference.cpp
    src/panels/logview.cpp
)

# Build stamp, regenerated on every build so the "modified" flag is honest.
# WriteBuildInfo.cmake only rewrites the header when something changed, so this
# costs one recompile per commit rather than one per build.
set(GXDEMO_BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp)
add_custom_target(gxdemo_build_info
    BYPRODUCTS ${GXDEMO_BUILD_INFO}
    COMMAND ${CMAKE_COMMAND}
        -D GXDEMO_VERSION=${PROJECT_VERSION}
        -D GXDEMO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D GXDEMO_TEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.hpp.in
        -D GXDEMO_OUTPUT=${GXDEMO_BUILD_INFO}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildInfo.cmake
    COMMENT "Stamping the build"
    VERBATIM
)
add_dependencies(gxdemo gxdemo_build_info)

# `cmake --build <dir> --target dist` drops a stamped archive in dist/ at the
# top of the tree, next to the sources rather than buried in a build directory.
# Not part of the default build: packaging is a thing you ask for.
if(WIN32)
    set(GXDEMO_PLATFORM "win64")
elseif(APPLE)
    set(GXDEMO_PLATFORM "macos")
else()
    set(GXDEMO_PLATFORM "linux")
endif()

add_custom_target(dist
    COMMAND ${CMAKE_COMMAND}
        -D GXDEMO_VERSION=${PROJECT_VERSION}
        -D GXDEMO_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -D GXDEMO_BINARY=$<TARGET_FILE:gxdemo>
        -D GXDEMO_PLATFORM=${GXDEMO_PLATFORM}
        -D GXDEMO_DIST=${PROJECT_SOURCE_DIR}/dist
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageApp.cmake
    DEPENDS gxdemo
    COMMENT "Packaging gxdemo into dist/"
    VERBATIM
)

target_include_directories(gxdemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(gxdemo PRIVATE gxnet_link wx::core wx::base)

if(WIN32)
    # Carries the Common Controls 6.0 manifest and the application icon; see
    # assets/gxdemo.rc for why the manifest is load-bearing rather than
    # cosmetic. The resource compiler needs to find both wx/msw/wx.rc and the
    # .ico next to the .rc, hence the two include paths.
    target_sources(gxdemo PRIVATE assets/gxdemo.rc)
    set_source_files_properties(assets/gxdemo.rc PROPERTIES
        INCLUDE_DIRECTORIES
            "${CMAKE_CURRENT_SOURCE_DIR}/assets;$<TARGET_PROPERTY:wx::core,INTERFACE_INCLUDE_DIRECTORIES>"
    )
endif()

target_compile_definitions(gxdemo PRIVATE NOMINMAX)

if(MSVC)
    target_compile_options(gxdemo PRIVATE /W4 /utf-8)
else()
    target_compile_options(gxdemo PRIVATE -Wall -Wextra)
endif()
