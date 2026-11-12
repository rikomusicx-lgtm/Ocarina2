# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2019 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# generate git/build information
include(GetSCMRev)

function(get_timestamp _var)
    string(TIMESTAMP timestamp UTC)
    set(${_var} "${timestamp}" PARENT_SCOPE)
endfunction()

get_timestamp(BUILD_DATE)

# Local release builds: a commit tagged like build-121 picks the tag up as the build number
# instead of requiring CI to pass LICHII_BUILD_TAG.
if (NOT LICHII_BUILD_TAG)
    execute_process(
        COMMAND git describe --exact-match --tags HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE exact_tag
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (exact_tag MATCHES "^build-[0-9]+$")
        set(LICHII_BUILD_TAG "${exact_tag}")
        message(STATUS "[GetSCMRev] LICHII_BUILD_TAG taken from the tag on HEAD: ${exact_tag}")
    endif()
endif()

# Ocarina2's own releases are numbered build-NNN. dist/ocarina2_version.txt carries the
# version for development builds, so the window and the log can name themselves. Versions
# are N, N.M or N.M.P without leading zeros; two-digit minors are allowed (N.10).
set(OCARINA2_VERSION_FILE "${CMAKE_SOURCE_DIR}/dist/ocarina2_version.txt")
if (EXISTS "${OCARINA2_VERSION_FILE}")
    file(STRINGS "${OCARINA2_VERSION_FILE}" OCARINA2_VERSION LIMIT_COUNT 1 REGEX "^[0-9]+([.][0-9]+)?([.][0-9]+)?$")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OCARINA2_VERSION_FILE}")
    # The log names the commit: reconfigure when HEAD moves (commit, checkout, merge, revert).
    if (EXISTS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
    endif()
endif()

# The Ocarina2 version in dist/ocarina2_version.txt wins for the user-facing name, so release
# builds read "Ocarina2 1.2", not "build-12". The LICHII_BUILD_TAG is still baked into the build
# for the update checker (release tags are compared against it).
if (OCARINA2_VERSION)
    set(BUILD_VERSION "${OCARINA2_VERSION}")
    if (LICHII_BUILD_TAG MATCHES "^build-[0-9]+$")
        set(IS_DEV_BUILD false)
    else()
        set(IS_DEV_BUILD true)
    endif()
elseif (LICHII_BUILD_TAG MATCHES "^build-[0-9]+$")
    set(BUILD_VERSION "${LICHII_BUILD_TAG}")
    set(IS_DEV_BUILD false)
elseif (DEFINED GIT_RELEASE)
    set(BUILD_VERSION "${GIT_TAG}")
    set(GIT_REFSPEC "${GIT_RELEASE}")
    set(IS_DEV_BUILD false)
else()
    string(SUBSTRING ${GIT_COMMIT} 0 10 BUILD_VERSION)
    set(BUILD_VERSION "${BUILD_VERSION}-${GIT_REFSPEC}")
    set(IS_DEV_BUILD true)
endif()

if (NIGHTLY_BUILD)
    set(IS_NIGHTLY_BUILD true)
else()
    set(IS_NIGHTLY_BUILD false)
endif()

set(GIT_DESC ${BUILD_VERSION})

# Generate cpp with Git revision from template

# Ocarina2 (rikomusicx-lgtm/Ocarina2, The Legend of Zelda: Ocarina of Time edition of Lichii).
set(BUILD_AUTO_UPDATE_STABLE_REPO "rikomusicx-lgtm/Ocarina2")
set(BUILD_AUTO_UPDATE_STABLE_API "api.github.com")
set(BUILD_AUTO_UPDATE_API_PATH "/repos/rikomusicx-lgtm/Ocarina2/releases?per_page=100")
set(BUILD_AUTO_UPDATE_WEBSITE "https://github.com")
set(BUILD_AUTO_UPDATE_REPO "rikomusicx-lgtm/Ocarina2")
if (NIGHTLY_BUILD)
    set(REPO_NAME "Ocarina2 Nightly")
else()
    set(REPO_NAME "Ocarina2")
endif()

set(BUILD_ID ${GIT_REFSPEC})
set(BUILD_FULLNAME "${REPO_NAME} ${BUILD_VERSION} ")
set(CXX_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

configure_file(scm_rev.cpp.in scm_rev.cpp @ONLY)
