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
  src/identity/catalog_manifest.cpp
  src/identity/canonical_identity.cpp
  src/ingestion/metrica_event_adapter.cpp
  src/ingestion/statsbomb_metadata_adapter.cpp
  src/ingestion/statsbomb_event_adapter.cpp
  src/ingestion/wyscout_metadata_adapter.cpp
  src/ingestion/wyscout_event_adapter.cpp
  src/persistence/match_review_file.cpp
  src/query/aggregation_query.cpp
  src/query/event_query.cpp
  src/reconciliation/catalog_validation.cpp
  src/reconciliation/entity_reconciliation.cpp
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
    tests/catalog_manifest_test.cpp
    tests/catalog_validation_test.cpp
    tests/canonical_identity_test.cpp
    tests/cli_command_parser_test.cpp
    tests/cli_renderer_test.cpp
    tests/coordinate_normalization_test.cpp
    tests/entity_reconciliation_test.cpp
    tests/entity_review_test.cpp
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

  set(EMBERDB_CLI_CATALOG_REVIEW
      "${CMAKE_CURRENT_BINARY_DIR}/emberdb-cli-catalog-review.json")
  add_test(
    NAME EmberDbCliCatalogTest.PreparesReview
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      ${EMBERDB_CLI_CATALOG_REVIEW}
      ${EMBERDB_CLI_CATALOG_REVIEW}.tmp
      ${EMBERDB_CLI_CATALOG_REVIEW}.lock
  )
  add_test(
    NAME EmberDbCliCatalogTest.InitializesReview
    COMMAND emberdb_cli catalog init --review ${EMBERDB_CLI_CATALOG_REVIEW}
  )
  set_tests_properties(EmberDbCliCatalogTest.InitializesReview PROPERTIES
    DEPENDS EmberDbCliCatalogTest.PreparesReview
    PASS_REGULAR_EXPRESSION "Review revision: 0"
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsCompetition
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity competition --canonical-id 20 --name "Premier League"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create competition"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsCompetition PROPERTIES
    DEPENDS EmberDbCliCatalogTest.InitializesReview
    PASS_REGULAR_EXPRESSION "Catalog revision 1: add competition 20"
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsSeason
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity season --canonical-id 30 --competition-id 20
      --name "2017/2018"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create season"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsSeason PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsCompetition
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsHomeTeam
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --canonical-id 1 --name "Arsenal"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create home team"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsHomeTeam PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsSeason
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsAwayTeam
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --canonical-id 2 --name "Leicester City"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create away team"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsAwayTeam PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsHomeTeam
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsPlayer
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 10 --name "Alex Forward"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create player"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsPlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsAwayTeam
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsMatch
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity match --canonical-id 100
      --competition "Premier League" --season "2017/2018"
      --home-team-id 1 --away-team-id 2
      --home-score 4 --away-score 3
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create match"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsMatch PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsPlayer
  )
  add_test(
    NAME EmberDbCliCatalogTest.MapsCompetition
    COMMAND emberdb_cli catalog map
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity competition --canonical-id 20
      --provider StatsBomb --provider-id 2
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Map competition"
  )
  set_tests_properties(EmberDbCliCatalogTest.MapsCompetition PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsMatch
  )
  add_test(
    NAME EmberDbCliCatalogTest.MapsSeason
    COMMAND emberdb_cli catalog map
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity season --canonical-id 30
      --provider StatsBomb --provider-id 44
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Map season"
  )
  set_tests_properties(EmberDbCliCatalogTest.MapsSeason PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MapsCompetition
  )
  add_test(
    NAME EmberDbCliCatalogTest.MapsTeam
    COMMAND emberdb_cli catalog map
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --canonical-id 1
      --provider StatsBomb --provider-id 10
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Map team"
  )
  set_tests_properties(EmberDbCliCatalogTest.MapsTeam PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MapsSeason
  )
  add_test(
    NAME EmberDbCliCatalogTest.MapsMatchLocalPlayer
    COMMAND emberdb_cli catalog map
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 10
      --provider Metrica --provider-id Player1 --provider-match-id 42
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Map player"
  )
  set_tests_properties(EmberDbCliCatalogTest.MapsMatchLocalPlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MapsTeam
  )
  add_test(
    NAME EmberDbCliCatalogTest.GeneratesPlayerCandidate
    COMMAND emberdb_cli catalog candidates generate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player
      --provider statsbomb
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/statsbomb_lineups.json
  )
  set_tests_properties(EmberDbCliCatalogTest.GeneratesPlayerCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MapsMatchLocalPlayer
    PASS_REGULAR_EXPRESSION "Added 1 new entity candidate"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsUnresolvedPlayerCandidate
    COMMAND emberdb_cli catalog candidates list
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player
      --status unresolved
  )
  set_tests_properties(
    EmberDbCliCatalogTest.ListsUnresolvedPlayerCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.GeneratesPlayerCandidate
    PASS_REGULAR_EXPRESSION "1.unresolved.player.1.StatsBomb:99.10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.InspectsPlayerCandidate
    COMMAND emberdb_cli catalog candidates inspect
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --candidate-id 1
  )
  set_tests_properties(EmberDbCliCatalogTest.InspectsPlayerCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsUnresolvedPlayerCandidate
    PASS_REGULAR_EXPRESSION "name.agreeing.Alex Forward.Alex Forward"
  )
  add_test(
    NAME EmberDbCliCatalogTest.AcceptsPlayerCandidate
    COMMAND emberdb_cli catalog candidates accept
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --candidate-id 1
      --actor "CTest reviewer"
      --source "offline fixtures"
      --reason "Verified player identity"
  )
  set_tests_properties(EmberDbCliCatalogTest.AcceptsPlayerCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.InspectsPlayerCandidate
    PASS_REGULAR_EXPRESSION
      "Accepted entity candidate 1: player StatsBomb:99 -> 10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.GeneratesTeamCandidate
    COMMAND emberdb_cli catalog candidates generate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team
      --provider statsbomb
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/statsbomb_reconciliation_matches.json
  )
  set_tests_properties(EmberDbCliCatalogTest.GeneratesTeamCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AcceptsPlayerCandidate
    PASS_REGULAR_EXPRESSION "Entity candidate IDs: 2"
  )
  add_test(
    NAME EmberDbCliCatalogTest.RejectsTeamCandidate
    COMMAND emberdb_cli catalog candidates reject
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --candidate-id 2
      --actor "CTest reviewer"
      --source "offline fixtures"
      --reason "Provider team requires manual review"
  )
  set_tests_properties(EmberDbCliCatalogTest.RejectsTeamCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.GeneratesTeamCandidate
    PASS_REGULAR_EXPRESSION
      "Rejected entity candidate 2: Provider team requires manual review"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsRejectedTeamCandidate
    COMMAND emberdb_cli catalog candidates list
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team
      --status rejected
  )
  set_tests_properties(
    EmberDbCliCatalogTest.ListsRejectedTeamCandidate PROPERTIES
    DEPENDS EmberDbCliCatalogTest.RejectsTeamCandidate
    PASS_REGULAR_EXPRESSION "2.rejected.team.1.StatsBomb:20.2"
  )
  add_test(
    NAME EmberDbCliCatalogTest.AddsDuplicatePlayer
    COMMAND emberdb_cli catalog add
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 11 --name "Alex F."
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Create duplicate player for maintenance coverage"
  )
  set_tests_properties(EmberDbCliCatalogTest.AddsDuplicatePlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsRejectedTeamCandidate
    PASS_REGULAR_EXPRESSION "Catalog revision 15: add player 11"
  )
  add_test(
    NAME EmberDbCliCatalogTest.MapsDuplicatePlayer
    COMMAND emberdb_cli catalog map
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 11
      --provider Wyscout --provider-id 25413
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Map duplicate player"
  )
  set_tests_properties(EmberDbCliCatalogTest.MapsDuplicatePlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.AddsDuplicatePlayer
    PASS_REGULAR_EXPRESSION "Catalog revision 16: map player 11"
  )
  add_test(
    NAME EmberDbCliCatalogTest.RenamesPlayer
    COMMAND emberdb_cli catalog rename
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 10 --name "Alex A. Forward"
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Correct canonical display name"
  )
  set_tests_properties(EmberDbCliCatalogTest.RenamesPlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MapsDuplicatePlayer
    PASS_REGULAR_EXPRESSION "Catalog revision 17: rename player 10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.MergesDuplicatePlayer
    COMMAND emberdb_cli catalog merge
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --canonical-id 11 --target-canonical-id 10
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Consolidate duplicate identity"
  )
  set_tests_properties(EmberDbCliCatalogTest.MergesDuplicatePlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.RenamesPlayer
    PASS_REGULAR_EXPRESSION "Catalog revision 18: merge player 11 -> 10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.DeprecatesAwayTeam
    COMMAND emberdb_cli catalog deprecate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --canonical-id 2
      --actor "CTest reviewer" --source "offline fixtures"
      --reason "Retire inactive catalog entry"
  )
  set_tests_properties(EmberDbCliCatalogTest.DeprecatesAwayTeam PROPERTIES
    DEPENDS EmberDbCliCatalogTest.MergesDuplicatePlayer
    PASS_REGULAR_EXPRESSION "Catalog revision 19: deprecate team 2"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsCatalogLifecycle
    COMMAND emberdb_cli catalog list --review ${EMBERDB_CLI_CATALOG_REVIEW}
  )
  set_tests_properties(EmberDbCliCatalogTest.ListsCatalogLifecycle PROPERTIES
    DEPENDS EmberDbCliCatalogTest.DeprecatesAwayTeam
    PASS_REGULAR_EXPRESSION "player.11.Alex F..merged.10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsRemappedPlayer
    COMMAND emberdb_cli catalog list --review ${EMBERDB_CLI_CATALOG_REVIEW}
  )
  set_tests_properties(EmberDbCliCatalogTest.ListsRemappedPlayer PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsCatalogLifecycle
    PASS_REGULAR_EXPRESSION "player.Wyscout:25413.10"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsDeprecatedTeam
    COMMAND emberdb_cli catalog list --review ${EMBERDB_CLI_CATALOG_REVIEW}
  )
  set_tests_properties(EmberDbCliCatalogTest.ListsDeprecatedTeam PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsRemappedPlayer
    PASS_REGULAR_EXPRESSION "team.2.Leicester City.deprecated.NULL"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ValidatesTeamCoverage
    COMMAND emberdb_cli catalog validate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --provider wyscout
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wyscout_teams.json
  )
  set_tests_properties(EmberDbCliCatalogTest.ValidatesTeamCoverage PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsDeprecatedTeam
    PASS_REGULAR_EXPRESSION "Unmapped exact matches: 1"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ReportsInactiveTeamMatch
    COMMAND emberdb_cli catalog validate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity team --provider wyscout
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wyscout_teams.json
  )
  set_tests_properties(EmberDbCliCatalogTest.ReportsInactiveTeamMatch PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ValidatesTeamCoverage
    PASS_REGULAR_EXPRESSION "Wyscout:1631.*Leicester City.*inactive_exact_match.*2"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ValidatesMappedPlayerDrift
    COMMAND emberdb_cli catalog validate
      --review ${EMBERDB_CLI_CATALOG_REVIEW}
      --entity player --provider statsbomb
      --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/statsbomb_lineups.json
  )
  set_tests_properties(EmberDbCliCatalogTest.ValidatesMappedPlayerDrift PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ReportsInactiveTeamMatch
    PASS_REGULAR_EXPRESSION "Mapped name mismatches: 1"
  )
  add_test(
    NAME EmberDbCliCatalogTest.ListsHistory
    COMMAND emberdb_cli catalog history --review ${EMBERDB_CLI_CATALOG_REVIEW}
  )
  set_tests_properties(EmberDbCliCatalogTest.ListsHistory PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ValidatesMappedPlayerDrift
    PASS_REGULAR_EXPRESSION "Catalog changes: 16"
  )
  add_test(
    NAME EmberDbCliCatalogTest.CleansReview
    COMMAND ${CMAKE_COMMAND} -E rm -rf
      ${EMBERDB_CLI_CATALOG_REVIEW}
      ${EMBERDB_CLI_CATALOG_REVIEW}.tmp
      ${EMBERDB_CLI_CATALOG_REVIEW}.lock
  )
  set_tests_properties(EmberDbCliCatalogTest.CleansReview PROPERTIES
    DEPENDS EmberDbCliCatalogTest.ListsHistory
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
