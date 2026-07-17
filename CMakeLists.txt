cmake_minimum_required(VERSION 3.20)

project(EmberDB VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(emberdb
  src/common/football_event_column.cpp
  src/ingestion/statsbomb_event_adapter.cpp
  src/query/aggregation_query.cpp
  src/query/event_query.cpp
  src/storage/football_event_table.cpp
)
add_library(EmberDB::emberdb ALIAS emberdb)

target_include_directories(emberdb PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(emberdb PRIVATE nlohmann_json::nlohmann_json)

function(emberdb_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()

emberdb_enable_warnings(emberdb)

add_executable(emberdb_cli src/main.cpp)
target_link_libraries(emberdb_cli PRIVATE EmberDB::emberdb)
emberdb_enable_warnings(emberdb_cli)

if(BUILD_TESTING)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(emberdb_tests
    tests/aggregation_query_test.cpp
    tests/statsbomb_event_adapter_test.cpp
    tests/football_event_table_test.cpp
    tests/event_query_test.cpp
  )
  target_link_libraries(emberdb_tests PRIVATE EmberDB::emberdb GTest::gtest_main)
  target_compile_definitions(emberdb_tests PRIVATE
    EMBERDB_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
  )
  emberdb_enable_warnings(emberdb_tests)

  include(GoogleTest)
  gtest_discover_tests(emberdb_tests)

  add_test(
    NAME EmberDbCliTest.FiltersAndProjectsFixture
    COMMAND emberdb_cli query
      --provider statsbomb
      --match-id 12345
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/complete_events.json
      --filter event_type=Pass
      --project player_name,minute,start_x,start_y
  )
  set_tests_properties(EmberDbCliTest.FiltersAndProjectsFixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Matched 1 event"
  )
endif()
