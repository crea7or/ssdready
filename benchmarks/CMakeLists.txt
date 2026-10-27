cmake_minimum_required(VERSION 3.24)
project(SsdReadyBenchmarks LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "These benchmarks require Windows, MSVC and the MFC component.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
include(FetchContent)
FetchContent_Declare(google_benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG 31c66f47f60d06790a4885a261d682ba339bd100) # v1.9.4
FetchContent_MakeAvailable(google_benchmark)

set(APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(APP_SOURCES
  pch.cpp mini_spy_manager.cpp drive_info.cpp process_name_resolver.cpp buffered_file.cpp history_format.cpp tree_item.cpp
  file.cpp file_error.cpp system_error.cpp
  string_utils.cpp named_object.cpp process_statistics.cpp process_write_statistics.cpp
  sortable_list_ctrl.cpp process_list_ctrl.cpp
  folder_tree_item.cpp monitoring_tree_ctrl.cpp app_log.cpp
  autostart_task.cpp interface_settings.cpp runtime_settings.cpp settings_registry.cpp)
list(TRANSFORM APP_SOURCES PREPEND "${APP_ROOT}/")
add_library(benchmark_runtime STATIC ${APP_SOURCES} ${APP_ROOT}/application_core.cpp benchmark_workload.cpp)
target_include_directories(benchmark_runtime PUBLIC "${APP_ROOT}" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(benchmark_runtime PUBLIC WIN32 UNICODE _UNICODE)
target_compile_options(benchmark_runtime PRIVATE /W4 /Gy)
target_link_libraries(benchmark_runtime PUBLIC benchmark::benchmark)

foreach(stage retrieve process)
  add_executable(${stage}_log_records_benchmark ${stage}_log_records_benchmark.cpp)
  target_link_libraries(${stage}_log_records_benchmark PRIVATE benchmark_runtime)
  target_compile_options(${stage}_log_records_benchmark PRIVATE /W4 /Gy)
  target_link_options(${stage}_log_records_benchmark PRIVATE /OPT:REF)
endforeach()

enable_testing()
add_executable(history_format_test history_format_test.cpp)
target_link_libraries(history_format_test PRIVATE benchmark_runtime)
target_compile_options(history_format_test PRIVATE /W4 /Gy)
target_link_options(history_format_test PRIVATE /OPT:REF)
add_test(NAME history_format COMMAND history_format_test)
set_tests_properties(history_format PROPERTIES TIMEOUT 60)

add_executable(drive_info_test drive_info_test.cpp)
target_link_libraries(drive_info_test PRIVATE benchmark_runtime)
target_compile_options(drive_info_test PRIVATE /W4 /Gy)
target_link_options(drive_info_test PRIVATE /OPT:REF)
add_test(NAME drive_info COMMAND drive_info_test)
set_tests_properties(drive_info PROPERTIES TIMEOUT 60)
add_test(NAME drive_info_native COMMAND drive_info_test --native)
set_tests_properties(drive_info_native PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)

add_executable(volume_enumeration_test volume_enumeration_test.cpp)
target_link_libraries(volume_enumeration_test PRIVATE benchmark_runtime)
target_compile_options(volume_enumeration_test PRIVATE /W4 /Gy)
target_link_options(volume_enumeration_test PRIVATE /OPT:REF)
add_test(NAME volume_enumeration COMMAND volume_enumeration_test)
set_tests_properties(volume_enumeration PROPERTIES TIMEOUT 30)

add_executable(process_resolver_cache_test process_resolver_cache_test.cpp)
target_link_libraries(process_resolver_cache_test PRIVATE benchmark_runtime)
target_compile_options(process_resolver_cache_test PRIVATE /W4 /Gy)
target_link_options(process_resolver_cache_test PRIVATE /OPT:REF)
add_test(NAME process_resolver_cache COMMAND process_resolver_cache_test)

foreach(stage retrieve process)
  foreach(cache warm cold)
    add_test(NAME ${stage}_${cache} COMMAND ${stage}_log_records_benchmark
      --items=128 --unique_paths=32 --processes=8 --cache=${cache} --benchmark_min_time=3x)
  endforeach()
endforeach()

add_executable(process_statistics_test process_statistics_test.cpp)
target_link_libraries(process_statistics_test PRIVATE benchmark_runtime)
target_compile_options(process_statistics_test PRIVATE /W4 /Gy)
target_link_options(process_statistics_test PRIVATE /OPT:REF)
add_test(NAME process_statistics COMMAND process_statistics_test)
set_tests_properties(process_statistics PROPERTIES TIMEOUT 60)

add_executable(batch_notification_test batch_notification_test.cpp)
target_link_libraries(batch_notification_test PRIVATE benchmark_runtime)
target_compile_options(batch_notification_test PRIVATE /W4 /Gy)
target_link_options(batch_notification_test PRIVATE /OPT:REF)
add_test(NAME batch_notification COMMAND batch_notification_test)
set_tests_properties(batch_notification PROPERTIES TIMEOUT 30)

add_executable(model_classes_test model_classes_test.cpp)
target_link_libraries(model_classes_test PRIVATE benchmark_runtime)
target_compile_options(model_classes_test PRIVATE /W4 /Gy)
target_link_options(model_classes_test PRIVATE /OPT:REF)
add_test(NAME model_classes COMMAND model_classes_test)
set_tests_properties(model_classes PROPERTIES TIMEOUT 60)

add_executable(settings_registry_test settings_registry_test.cpp)
target_link_libraries(settings_registry_test PRIVATE benchmark_runtime)
target_compile_options(settings_registry_test PRIVATE /W4 /Gy)
target_link_options(settings_registry_test PRIVATE /OPT:REF)
add_test(NAME settings_registry COMMAND settings_registry_test)
set_tests_properties(settings_registry PROPERTIES TIMEOUT 60)

add_executable(mini_spy_manager_test mini_spy_manager_test.cpp)
target_link_libraries(mini_spy_manager_test PRIVATE benchmark_runtime)
target_compile_options(mini_spy_manager_test PRIVATE /W4 /Gy)
target_link_options(mini_spy_manager_test PRIVATE /OPT:REF)
add_test(NAME mini_spy_manager COMMAND mini_spy_manager_test)
set_tests_properties(mini_spy_manager PROPERTIES TIMEOUT 60)

add_executable(worker_lifecycle_test worker_lifecycle_test.cpp)
target_link_libraries(worker_lifecycle_test PRIVATE benchmark_runtime)
target_compile_options(worker_lifecycle_test PRIVATE /W4 /Gy)
target_link_options(worker_lifecycle_test PRIVATE /OPT:REF)
add_test(NAME worker_lifecycle COMMAND worker_lifecycle_test)
set_tests_properties(worker_lifecycle PROPERTIES TIMEOUT 30)

add_executable(file_utilities_test file_utilities_test.cpp)
target_link_libraries(file_utilities_test PRIVATE benchmark_runtime)
target_compile_options(file_utilities_test PRIVATE /W4 /Gy)
target_link_options(file_utilities_test PRIVATE /OPT:REF)
add_test(NAME file_utilities COMMAND file_utilities_test)
set_tests_properties(file_utilities PROPERTIES TIMEOUT 60)

add_executable(ui_controls_test ui_controls_test.cpp ${APP_ROOT}/SsdReady.rc
  ${APP_ROOT}/styled_button.cpp ${APP_ROOT}/dialog_base.cpp ${APP_ROOT}/resizable_dialog.cpp
  ${APP_ROOT}/hyperlink_ctrl.cpp ${APP_ROOT}/about_dialog.cpp ${APP_ROOT}/drive_size_combo_box.cpp
  ${APP_ROOT}/drive_list_ctrl.cpp ${APP_ROOT}/log_viewer_dialog.cpp ${APP_ROOT}/colored_list_box.cpp)
target_link_libraries(ui_controls_test PRIVATE benchmark_runtime)
target_compile_options(ui_controls_test PRIVATE /W4 /Gy)
target_link_options(ui_controls_test PRIVATE /OPT:REF)
add_test(NAME ui_controls COMMAND ui_controls_test)
set_tests_properties(ui_controls PROPERTIES TIMEOUT 60)

add_executable(installer_paths_test installer_paths_test.cpp ${APP_ROOT}/DriverInstaller/installer_paths.cpp)
target_compile_definitions(installer_paths_test PRIVATE UNICODE _UNICODE)
target_include_directories(installer_paths_test PRIVATE ${APP_ROOT})
target_compile_options(installer_paths_test PRIVATE /W4)
add_test(NAME installer_paths COMMAND installer_paths_test)
set_tests_properties(installer_paths PROPERTIES TIMEOUT 30)
