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
  src/common/coordinate_normalization.cpp
  src/common/football_event_column.cpp
  src/ingestion/metrica_event_adapter.cpp
  src/ingestion/statsbomb_event_adapter.cpp
  src/query/aggregation_query.cpp
  src/query/event_query.cpp
  src/storage/football_event_file.cpp
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
    tests/coordinate_normalization_test.cpp
    tests/football_event_file_test.cpp
    tests/metrica_event_adapter_test.cpp
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
      --project player_name,minute,start_x,start_y,source_start_x,source_start_y
  )
  set_tests_properties(EmberDbCliTest.FiltersAndProjectsFixture PROPERTIES
    PASS_REGULAR_EXPRESSION "35.4167"
  )

  add_test(
    NAME EmberDbCliTest.AggregatesAndGroupsFixture
    COMMAND emberdb_cli query
      --provider statsbomb
      --match-id 12345
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/complete_events.json
      --aggregate "count(*)"
      --aggregate "avg(start_x)"
      --group-by event_type
  )
  set_tests_properties(EmberDbCliTest.AggregatesAndGroupsFixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Result rows: 2"
  )

  add_test(
    NAME EmberDbCliTest.ImportsAndQueriesMetricaFixture
    COMMAND emberdb_cli query
      --provider metrica
      --match-id 42
      --home-first-half-direction left-to-right
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/metrica_events.csv
      --filter event_type=Pass
      --project provider,player_name,minute,start_x,start_y,source_start_x,source_start_y
  )
  set_tests_properties(EmberDbCliTest.ImportsAndQueriesMetricaFixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Metrica"
  )

  add_test(
    NAME EmberDbCliTest.RejectsMixedProjectionAndAggregation
    COMMAND emberdb_cli query
      --provider statsbomb
      --match-id 12345
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/complete_events.json
      --project event_type
      --aggregate "count(*)"
  )
  set_tests_properties(EmberDbCliTest.RejectsMixedProjectionAndAggregation PROPERTIES
    WILL_FAIL TRUE
  )

  add_test(
    NAME EmberDbCliTest.RejectsTextSum
    COMMAND emberdb_cli query
      --provider statsbomb
      --match-id 12345
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/complete_events.json
      --aggregate "sum(event_type)"
  )
  set_tests_properties(EmberDbCliTest.RejectsTextSum PROPERTIES
    WILL_FAIL TRUE
  )

  set(EMBERDB_CLI_TEST_DATABASE
      "${CMAKE_CURRENT_BINARY_DIR}/emberdb-cli-persistence-test.ember")
  add_test(
    NAME EmberDbCliPersistenceTest.PreparesOutput
    COMMAND ${CMAKE_COMMAND} -E rm -f
      ${EMBERDB_CLI_TEST_DATABASE}
      ${EMBERDB_CLI_TEST_DATABASE}.tmp
  )
  add_test(
    NAME EmberDbCliPersistenceTest.ImportsDatabase
    COMMAND emberdb_cli import
      --provider statsbomb
      --match-id 12345
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/complete_events.json
      --output ${EMBERDB_CLI_TEST_DATABASE}
  )
  set_tests_properties(EmberDbCliPersistenceTest.ImportsDatabase PROPERTIES
    DEPENDS EmberDbCliPersistenceTest.PreparesOutput
    PASS_REGULAR_EXPRESSION "Saved database:"
  )
  add_test(
    NAME EmberDbCliPersistenceTest.QueriesDatabase
    COMMAND emberdb_cli query
      --database ${EMBERDB_CLI_TEST_DATABASE}
      --filter event_type=Pass
      --project player_name,minute,start_x,start_y,source_start_x,source_start_y
  )
  set_tests_properties(EmberDbCliPersistenceTest.QueriesDatabase PROPERTIES
    DEPENDS EmberDbCliPersistenceTest.ImportsDatabase
    PASS_REGULAR_EXPRESSION "35.4167"
  )
  add_test(
    NAME EmberDbCliPersistenceTest.AggregatesDatabase
    COMMAND emberdb_cli query
      --database ${EMBERDB_CLI_TEST_DATABASE}
      --group-by event_type
      --aggregate "count(*)"
  )
  set_tests_properties(EmberDbCliPersistenceTest.AggregatesDatabase PROPERTIES
    DEPENDS EmberDbCliPersistenceTest.ImportsDatabase
    PASS_REGULAR_EXPRESSION "Result rows: 2"
  )
  add_test(
    NAME EmberDbCliPersistenceTest.CleansOutput
    COMMAND ${CMAKE_COMMAND} -E rm -f
      ${EMBERDB_CLI_TEST_DATABASE}
      ${EMBERDB_CLI_TEST_DATABASE}.tmp
  )
  set_tests_properties(EmberDbCliPersistenceTest.CleansOutput PROPERTIES
    DEPENDS "EmberDbCliPersistenceTest.QueriesDatabase;EmberDbCliPersistenceTest.AggregatesDatabase"
  )
endif()
