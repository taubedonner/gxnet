# Transport layer. The mock builds everywhere; the BCS transport is Windows
# only and is added to the same target rather than a separate one, so callers
# link one library regardless of platform.

add_library(gxnet_link
    src/transport.cpp
    src/mock.cpp
    src/worker.cpp
    src/operations.cpp
    # Always compiled: off Windows it provides the stubs that make
    # BcsTransport::available() answer false instead of failing to link.
    src/bcs.cpp
)

target_include_directories(gxnet_link
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(gxnet_link PUBLIC gxnet)

find_package(Threads REQUIRED)
target_link_libraries(gxnet_link PUBLIC Threads::Threads)

if(WIN32)
    target_sources(gxnet_link PRIVATE
        src/dispatch.cpp
    )
    # ole32 for CoInitializeEx/CoCreateInstance, oleaut32 for the BSTR and
    # VARIANT helpers, uuid for IID_IDispatch and GUID_NULL. MSVC pulls the
    # last one in on its own; MinGW does not, and a cross build fails at link
    # time without it.
    target_link_libraries(gxnet_link PRIVATE ole32 oleaut32 uuid)
endif()

if(MSVC)
    # See the top-level CMakeLists for why /utf-8 matters here.
    target_compile_options(gxnet_link PRIVATE /W4 /utf-8)
else()
    target_compile_options(gxnet_link PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Included here rather than relying on the top level: this directory is added
# before the top-level include(GNUInstallDirs) runs, so the variables would
# otherwise be empty and install() would fail with no destination.
include(GNUInstallDirs)
install(TARGETS gxnet_link ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
