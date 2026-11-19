cmake_minimum_required(VERSION 3.24)
project(SsdReadyHistoryExport LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "history_export requires Windows, MSVC and the MFC component.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

# build_version.h is generated on every build from cmake/version.cmake + git.
set(VERSION_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_target(ssdready_version
  COMMAND "${CMAKE_COMMAND}" "-DOUT_FILE=${VERSION_DIR}/build_version.h" -P "${APP_ROOT}/cmake/GenerateVersion.cmake"
  BYPRODUCTS "${VERSION_DIR}/build_version.h"
  COMMENT "Updating build_version.h"
  VERBATIM)

# The history codec and tree model link against the rest of the app's model code.
set(APP_SOURCES
  pch.cpp mini_spy_manager.cpp drive_info.cpp process_name_resolver.cpp buffered_file.cpp history_format.cpp tree_item.cpp
  file.cpp file_error.cpp system_error.cpp
  string_utils.cpp named_object.cpp process_statistics.cpp process_write_statistics.cpp
  folder_tree_item.cpp monitoring_tree_ctrl.cpp app_log.cpp application_core.cpp
  autostart_task.cpp interface_settings.cpp runtime_settings.cpp settings_registry.cpp)
list(TRANSFORM APP_SOURCES PREPEND "${APP_ROOT}/")

add_executable(history_export history_export.cpp ${APP_SOURCES})
add_dependencies(history_export ssdready_version)
target_include_directories(history_export PRIVATE "${APP_ROOT}" "${VERSION_DIR}")
target_compile_definitions(history_export PRIVATE WIN32 UNICODE _UNICODE)
target_compile_options(history_export PRIVATE /W4 /Gy)
target_link_options(history_export PRIVATE /OPT:REF)
