# Writes build_info.hpp: version, build number, commit, date.
#
# Run as a script (-P) from a custom target, not at configure time, so the
# "modified" flag reflects the tree as it is when the build runs rather than as
# it was when CMake last ran. configure_file only rewrites the file when the
# contents change, so a build that changes nothing recompiles nothing.
#
# Expects: GXDEMO_VERSION, GXDEMO_SOURCE_DIR, GXDEMO_TEMPLATE, GXDEMO_OUTPUT.

include("${CMAKE_CURRENT_LIST_DIR}/BuildStamp.cmake")
gxnet_build_stamp()

configure_file("${GXDEMO_TEMPLATE}" "${GXDEMO_OUTPUT}" @ONLY)
