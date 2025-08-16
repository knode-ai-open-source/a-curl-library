cmake_minimum_required(VERSION 3.20)
project(a_curl_library_tests LANGUAGES C)

enable_testing()

find_package(a_curl_library CONFIG REQUIRED)

# Optional system libs
find_library(M_LIB  m)
find_library(RT_LIB rt)

# Helper: one C file -> one test exe
function(make_test name src)
  add_executable(${name} "${src}")
  set_target_properties(${name} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
  )

  target_link_libraries(${name} PRIVATE
    a_curl_library::a_curl_library
  )

  if(M_LIB)
    target_link_libraries(${name} PRIVATE ${M_LIB})
  endif()
  if(RT_LIB)
    target_link_libraries(${name} PRIVATE ${RT_LIB})
  endif()

  if(MSVC)
    target_compile_options(${name} PRIVATE /W4)
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  add_test(NAME ${name} COMMAND ${name})
endfunction()

# Tests (no network required)
make_test(test_worker_pool      "${CMAKE_CURRENT_SOURCE_DIR}/src/test_worker_pool.c")
make_test(test_rate_manager     "${CMAKE_CURRENT_SOURCE_DIR}/src/test_rate_manager.c")
make_test(test_curl_resource    "${CMAKE_CURRENT_SOURCE_DIR}/src/test_curl_resource.c")
make_test(test_request_json     "${CMAKE_CURRENT_SOURCE_DIR}/src/test_request_json.c")
make_test(test_request_headers       "${CMAKE_CURRENT_SOURCE_DIR}/src/test_request_headers.c")
make_test(test_sinks                 "${CMAKE_CURRENT_SOURCE_DIR}/src/test_sinks.c")
make_test(test_rate_manager_hp_429   "${CMAKE_CURRENT_SOURCE_DIR}/src/test_rate_manager_hp_429.c")
make_test(test_curl_resource_async   "${CMAKE_CURRENT_SOURCE_DIR}/src/test_curl_resource_async.c")
make_test(test_event_loop_cancel     "${CMAKE_CURRENT_SOURCE_DIR}/src/test_event_loop_cancel.c")
make_test(test_event_loop_priority   "${CMAKE_CURRENT_SOURCE_DIR}/src/test_event_loop_priority.c")