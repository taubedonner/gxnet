# Where the build stamp comes from.
#
# Included by both WriteBuildInfo.cmake (the header the About box reads) and
# PackageApp.cmake (the archive name), so the two can never disagree about what
# build they are describing.
#
# Sets, in the caller's scope:
#   GXDEMO_BUILD    commits behind HEAD, "0" outside a checkout
#   GXDEMO_COMMIT   short hash, with ", modified" when the tree is not clean
#   GXDEMO_DATE     UTC date
#   GXDEMO_SLUG     the same facts in a form safe for a filename
#
# Expects GXDEMO_SOURCE_DIR to be set.

function(gxnet_build_stamp)
    set(_build "0")
    set(_commit "no git checkout")
    set(_slug "nogit")

    find_package(Git QUIET)

    if(Git_FOUND AND IS_DIRECTORY "${GXDEMO_SOURCE_DIR}/.git")
        # Build number: how many commits are behind this one. Monotonic, tied to
        # the history rather than to how many times someone pressed build, and it
        # means the same thing on every machine.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
            WORKING_DIRECTORY "${GXDEMO_SOURCE_DIR}"
            OUTPUT_VARIABLE _count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_count)
            set(_build "${_count}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${GXDEMO_SOURCE_DIR}"
            OUTPUT_VARIABLE _hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(NOT _hash)
            # A repository exists but nothing is committed to it yet, which is a
            # different thing from building outside a checkout.
            set(_commit "no commits yet")
            set(_slug "nocommit")
        else()
            set(_commit "${_hash}")
            set(_slug "${_hash}")

            # Uncommitted changes matter more than the hash: a binary built from
            # a dirty tree cannot be reproduced from the commit it names.
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                WORKING_DIRECTORY "${GXDEMO_SOURCE_DIR}"
                OUTPUT_VARIABLE _dirty
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_dirty)
                set(_commit "${_commit}, modified")
                set(_slug "${_slug}-dirty")
            endif()
        endif()
    endif()

    string(TIMESTAMP _date "%Y-%m-%d" UTC)

    set(GXDEMO_BUILD "${_build}" PARENT_SCOPE)
    set(GXDEMO_COMMIT "${_commit}" PARENT_SCOPE)
    set(GXDEMO_DATE "${_date}" PARENT_SCOPE)
    set(GXDEMO_SLUG "${_slug}" PARENT_SCOPE)
endfunction()
