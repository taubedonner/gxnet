# Cross-compiling gxdemo for Windows with llvm-mingw: clang, lld, libc++ and
# compiler-rt over the mingw-w64 CRT.
#
# Why this rather than the GCC cross-compiler:
#
#   * The runtime links in statically without a fight. GCC's mingw drags
#     libgcc_s_seh-1.dll and libstdc++-6.dll behind every shared library it
#     produces; compiler-rt and libc++ do not.
#   * clang's diagnostics, and lld's link times.
#
# What it does not change: the three upstream projects that assume headers
# libstdc++ used to pull in transitively still need them forced, and libc++ is
# stricter about that than libstdc++ ever was.
#
# Point GXNET_LLVM_MINGW_ROOT at an unpacked llvm-mingw release, or let it find
# one under .incoming/toolchains.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT GXNET_LLVM_MINGW_ROOT)
    if(DEFINED ENV{LLVM_MINGW_ROOT})
        set(GXNET_LLVM_MINGW_ROOT $ENV{LLVM_MINGW_ROOT})
    else()
        file(GLOB _candidates
             "${CMAKE_CURRENT_LIST_DIR}/../../.incoming/toolchains/llvm-mingw-*")
        list(SORT _candidates)
        list(REVERSE _candidates)
        if(_candidates)
            list(GET _candidates 0 GXNET_LLVM_MINGW_ROOT)
        endif()
    endif()
endif()

if(NOT GXNET_LLVM_MINGW_ROOT OR NOT EXISTS "${GXNET_LLVM_MINGW_ROOT}/bin")
    message(FATAL_ERROR
        "llvm-mingw not found. Download a release from "
        "https://github.com/mstorsjo/llvm-mingw/releases, unpack it, and pass "
        "-DGXNET_LLVM_MINGW_ROOT=<path> (or set LLVM_MINGW_ROOT).")
endif()

set(_triple x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   "${GXNET_LLVM_MINGW_ROOT}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${GXNET_LLVM_MINGW_ROOT}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER  "${GXNET_LLVM_MINGW_ROOT}/bin/${_triple}-windres")
set(CMAKE_AR           "${GXNET_LLVM_MINGW_ROOT}/bin/llvm-ar")
set(CMAKE_RANLIB       "${GXNET_LLVM_MINGW_ROOT}/bin/llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH "${GXNET_LLVM_MINGW_ROOT}/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static libc++ and compiler-rt: one executable, no runtime DLLs to ship
# alongside it. Applied to shared libraries too -- a DLL that links the runtime
# dynamically would drag it back in no matter how the executable was linked.
set(_static_runtime "-static -static-libgcc")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_static_runtime}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_static_runtime}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_static_runtime}")
