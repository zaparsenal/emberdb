cmake_minimum_required(VERSION 3.20)

project(EmberDB VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)

option(EMBERDB_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(EMBERDB_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
      -fsanitize=address,undefined
      -fno-omit-frame-pointer
    )
    add_link_options(-fsanitize=address,undefined)
  else()
    message(FATAL_ERROR
      "EMBERDB_ENABLE_SANITIZERS requires a Clang or GCC-compatible compiler")
  endif()
endif()

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(emberdb
  src/common/coordinate_normalization.cpp
  src/common/football_event.cpp
  src/common/football_event_column.cpp
  src/identity/canonical_identity.cpp
  src/ingestion/metrica_event_adapter.cpp
  src/ingestion/statsbomb_metadata_adapter.cpp
  src/ingestion/statsbomb_event_adapter.cpp
  src/ingestion/wyscout_metadata_adapter.cpp
  src/ingestion/wyscout_event_adapter.cpp
  src/persistence/match_review_file.cpp
  src/query/aggregation_query.cpp
  src/query/event_query.cpp
  src/reconciliation/match_reconciliation.cpp
  src/reconciliation/match_review.cpp
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

add_library(emberdb_cli_support
  src/cli/command_parser.cpp
  src/cli/executor.cpp
  src/cli/renderer.cpp
)
target_include_directories(emberdb_cli_support PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(emberdb_cli_support PUBLIC EmberDB::emberdb)
emberdb_enable_warnings(emberdb_cli_support)

add_executable(emberdb_cli src/main.cpp)
target_link_libraries(emberdb_cli PRIVATE emberdb_cli_support)
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
    tests/canonical_identity_test.cpp
    tests/cli_command_parser_test.cpp
    tests/cli_renderer_test.cpp
    tests/coordinate_normalization_test.cpp
    tests/football_event_validation_test.cpp
    tests/football_event_file_test.cpp
    tests/metrica_event_adapter_test.cpp
    tests/match_review_file_test.cpp
    tests/match_reconciliation_test.cpp
    tests/match_review_test.cpp
    tests/statsbomb_metadata_adapter_test.cpp
    tests/statsbomb_event_adapter_test.cpp
    tests/wyscout_metadata_adapter_test.cpp
    tests/wyscout_event_adapter_test.cpp
    tests/football_event_table_test.cpp
    tests/event_query_test.cpp
  )
  target_link_libraries(emberdb_tests PRIVATE
    EmberDB::emberdb
    emberdb_cli_support
    GTest::gtest_main
  )
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
    NAME EmberDbCliTest.CountsMetricaPlayerNames
    COMMAND emberdb_cli import
      --provider metrica
      --match-id 42
      --home-first-half-direction left-to-right
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/metrica_events.csv
  )
  set_tests_properties(EmberDbCliTest.CountsMetricaPlayerNames PROPERTIES
    PASS_REGULAR_EXPRESSION "Events with player data: 3"
  )

  add_test(
    NAME EmberDbCliTest.ImportsAndQueriesWyscoutFixture
    COMMAND emberdb_cli query
      --provider wyscout
      --match-id 2576335
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wyscout_events.json
      --filter event_type=Pass
      --project provider,player_id,minute,start_x,start_y,source_start_x,source_start_y
  )
  set_tests_properties(EmberDbCliTest.ImportsAndQueriesWyscoutFixture PROPERTIES
    PASS_REGULAR_EXPRESSION "Wyscout"
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

  set(EMBERDB_CLI_REJECT_REVIEW
      "${CMAKE_CURRENT_BINARY_DIR}/emberdb-cli-reject-review.json")
  set(EMBERDB_CLI_ACCEPT_REVIEW
      "${CMAKE_CURRENT_BINARY_DIR}/emberdb-cli-accept-review.json")
  set(EMBERDB_REVIEW_FIXTURE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/match_review_store.json")

  add_test(
    NAME EmberDbCliReconciliationTest.PreparesRejectReview
    COMMAND ${CMAKE_COMMAND} -E rm -f
      ${EMBERDB_CLI_REJECT_REVIEW}
      ${EMBERDB_CLI_REJECT_REVIEW}.tmp
  )
  add_test(
    NAME EmberDbCliReconciliationTest.CopiesRejectReview
    COMMAND ${CMAKE_COMMAND} -E copy
      ${EMBERDB_REVIEW_FIXTURE}
      ${EMBERDB_CLI_REJECT_REVIEW}
  )
  set_tests_properties(EmberDbCliReconciliationTest.CopiesRejectReview PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.PreparesRejectReview
  )
  add_test(
    NAME EmberDbCliReconciliationTest.GeneratesCandidate
    COMMAND emberdb_cli reconcile generate
      --review ${EMBERDB_CLI_REJECT_REVIEW}
      --left-provider statsbomb
      --left-input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/statsbomb_reconciliation_matches.json
      --right-provider wyscout
      --right-input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wyscout_matches.json
  )
  set_tests_properties(EmberDbCliReconciliationTest.GeneratesCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.CopiesRejectReview
    PASS_REGULAR_EXPRESSION "Added 1 new candidate"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.ListsUnresolvedCandidate
    COMMAND emberdb_cli reconcile list
      --review ${EMBERDB_CLI_REJECT_REVIEW}
      --status unresolved
  )
  set_tests_properties(EmberDbCliReconciliationTest.ListsUnresolvedCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.GeneratesCandidate
    PASS_REGULAR_EXPRESSION "1.unresolved.0.85"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.InspectsCandidateEvidence
    COMMAND emberdb_cli reconcile inspect
      --review ${EMBERDB_CLI_REJECT_REVIEW}
      --candidate-id 1
  )
  set_tests_properties(EmberDbCliReconciliationTest.InspectsCandidateEvidence PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.ListsUnresolvedCandidate
    PASS_REGULAR_EXPRESSION "home_team.agreeing.10.1609.1"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.RejectsCandidate
    COMMAND emberdb_cli reconcile reject
      --review ${EMBERDB_CLI_REJECT_REVIEW}
      --candidate-id 1
      --actor "CTest reviewer"
      --source "offline fixtures"
      --reason "Provider correction"
  )
  set_tests_properties(EmberDbCliReconciliationTest.RejectsCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.InspectsCandidateEvidence
    PASS_REGULAR_EXPRESSION "Rejected candidate 1: Provider correction"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.ListsRejectedCandidate
    COMMAND emberdb_cli reconcile list
      --review ${EMBERDB_CLI_REJECT_REVIEW}
      --status rejected
  )
  set_tests_properties(EmberDbCliReconciliationTest.ListsRejectedCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.RejectsCandidate
    PASS_REGULAR_EXPRESSION "1.rejected.0.85"
  )

  add_test(
    NAME EmberDbCliReconciliationTest.PreparesAcceptReview
    COMMAND ${CMAKE_COMMAND} -E rm -f
      ${EMBERDB_CLI_ACCEPT_REVIEW}
      ${EMBERDB_CLI_ACCEPT_REVIEW}.tmp
  )
  add_test(
    NAME EmberDbCliReconciliationTest.CopiesAcceptReview
    COMMAND ${CMAKE_COMMAND} -E copy
      ${EMBERDB_REVIEW_FIXTURE}
      ${EMBERDB_CLI_ACCEPT_REVIEW}
  )
  set_tests_properties(EmberDbCliReconciliationTest.CopiesAcceptReview PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.PreparesAcceptReview
  )
  add_test(
    NAME EmberDbCliReconciliationTest.GeneratesAcceptedCandidate
    COMMAND emberdb_cli reconcile generate
      --review ${EMBERDB_CLI_ACCEPT_REVIEW}
      --left-provider statsbomb
      --left-input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/statsbomb_reconciliation_matches.json
      --right-provider wyscout
      --right-input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wyscout_matches.json
  )
  set_tests_properties(EmberDbCliReconciliationTest.GeneratesAcceptedCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.CopiesAcceptReview
    PASS_REGULAR_EXPRESSION "Candidate IDs: 1"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.AcceptsCandidate
    COMMAND emberdb_cli reconcile accept
      --review ${EMBERDB_CLI_ACCEPT_REVIEW}
      --candidate-id 1
      --canonical-match-id 100
      --actor "CTest reviewer"
      --source "offline fixtures"
      --reason "Fixture metadata agrees"
  )
  set_tests_properties(EmberDbCliReconciliationTest.AcceptsCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.GeneratesAcceptedCandidate
    PASS_REGULAR_EXPRESSION "Accepted candidate 1 as canonical match 100"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.InspectsAcceptedCandidate
    COMMAND emberdb_cli reconcile inspect
      --review ${EMBERDB_CLI_ACCEPT_REVIEW}
      --candidate-id 1
  )
  set_tests_properties(EmberDbCliReconciliationTest.InspectsAcceptedCandidate PROPERTIES
    DEPENDS EmberDbCliReconciliationTest.AcceptsCandidate
    PASS_REGULAR_EXPRESSION "Canonical match: 100"
  )
  add_test(
    NAME EmberDbCliReconciliationTest.CleansReviews
    COMMAND ${CMAKE_COMMAND} -E rm -f
      ${EMBERDB_CLI_REJECT_REVIEW}
      ${EMBERDB_CLI_REJECT_REVIEW}.tmp
      ${EMBERDB_CLI_ACCEPT_REVIEW}
      ${EMBERDB_CLI_ACCEPT_REVIEW}.tmp
  )
  set_tests_properties(EmberDbCliReconciliationTest.CleansReviews PROPERTIES
    DEPENDS "EmberDbCliReconciliationTest.ListsRejectedCandidate;EmberDbCliReconciliationTest.InspectsAcceptedCandidate"
  )
endif()
