# Generates build_version.h from cmake/version.cmake and git state.
#
#   cmake -DOUT_FILE=<dir>/build_version.h -P cmake/GenerateVersion.cmake
#
# Called before every build by SsdReady.vcxproj (pre-build event) and by
# benchmarks/CMakeLists.txt. The file is rewritten only when its content
# changes, so unchanged builds do not recompile anything.
cmake_minimum_required(VERSION 3.20)

if(NOT OUT_FILE)
  message(FATAL_ERROR "GenerateVersion.cmake: pass -DOUT_FILE=<path to build_version.h>")
endif()
file(TO_CMAKE_PATH "${OUT_FILE}" OUT_FILE)
get_filename_component(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/version.cmake")

set(commit_count 0)
set(SSDREADY_GIT_HASH "nogit")

find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
                  WORKING_DIRECTORY "${SRC_DIR}"
                  OUTPUT_VARIABLE git_count OUTPUT_STRIP_TRAILING_WHITESPACE
                  RESULT_VARIABLE git_result ERROR_QUIET)
  if(git_result EQUAL 0 AND git_count MATCHES "^[0-9]+$")
    set(commit_count ${git_count})

    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
                    WORKING_DIRECTORY "${SRC_DIR}"
                    OUTPUT_VARIABLE SSDREADY_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

    # Tracked files modified since the last commit -> mark the build as dirty.
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
                    WORKING_DIRECTORY "${SRC_DIR}"
                    OUTPUT_VARIABLE git_status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT git_status STREQUAL "")
      string(APPEND SSDREADY_GIT_HASH "-dirty")
    endif()

    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --is-shallow-repository
                    WORKING_DIRECTORY "${SRC_DIR}"
                    OUTPUT_VARIABLE git_shallow OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(git_shallow STREQUAL "true")
      message(WARNING "Shallow git clone: the commit count (build number) is wrong. Fetch the full history.")
    endif()
  else()
    message(WARNING "Not a git checkout: the build number falls back to ${SSDREADY_BUILD_OFFSET}.")
  endif()
else()
  message(WARNING "git not found: the build number falls back to ${SSDREADY_BUILD_OFFSET}.")
endif()

math(EXPR SSDREADY_VERSION_BUILD "${SSDREADY_BUILD_OFFSET} + ${commit_count}")
if(SSDREADY_VERSION_BUILD GREATER 65535)
  message(FATAL_ERROR "Build number ${SSDREADY_VERSION_BUILD} does not fit in a 16-bit VERSIONINFO field.")
endif()

set(SSDREADY_VERSION_STRING
    "${SSDREADY_VERSION_MAJOR}.${SSDREADY_VERSION_MINOR}.${SSDREADY_VERSION_BUILD}")
set(SSDREADY_VERSION_FULL_STRING "${SSDREADY_VERSION_STRING}+${SSDREADY_GIT_HASH}")

configure_file("${CMAKE_CURRENT_LIST_DIR}/build_version.h.in" "${OUT_FILE}" @ONLY NEWLINE_STYLE CRLF)
message(STATUS "SsdReady version ${SSDREADY_VERSION_FULL_STRING}")
