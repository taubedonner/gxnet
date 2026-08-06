# Puts the built executable into dist/ as a stamped archive.
#
# The name carries everything needed to identify a binary someone sends back
# with a bug report: version, build number, commit, platform. A "-dirty" in it
# means the tree had uncommitted changes and the commit alone will not reproduce
# the file.
#
# `cmake -E tar` rather than zip(1): the same command works on every platform
# the project builds on, including a Windows machine with no unix tools.
#
# Expects: GXDEMO_VERSION, GXDEMO_SOURCE_DIR, GXDEMO_BINARY, GXDEMO_PLATFORM,
#          GXDEMO_DIST.

include("${CMAKE_CURRENT_LIST_DIR}/BuildStamp.cmake")
gxnet_build_stamp()

if(NOT EXISTS "${GXDEMO_BINARY}")
    message(FATAL_ERROR "nothing to package: ${GXDEMO_BINARY} does not exist")
endif()

get_filename_component(_name "${GXDEMO_BINARY}" NAME)
get_filename_component(_dir "${GXDEMO_BINARY}" DIRECTORY)

set(_archive
    "${GXDEMO_DIST}/gxdemo-${GXDEMO_VERSION}-b${GXDEMO_BUILD}-${GXDEMO_SLUG}-${GXDEMO_PLATFORM}.zip")

file(MAKE_DIRECTORY "${GXDEMO_DIST}")

# Run from the directory holding the binary so the archive contains the file
# and not a tree of build directories.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${_archive}" --format=zip -- "${_name}"
    WORKING_DIRECTORY "${_dir}"
    RESULT_VARIABLE _result
)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "packaging failed")
endif()

message(STATUS "packaged ${_archive}")
