#include "cli/command_parser.h"

#include <array>
#include <chrono>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

namespace {

TEST(CliCommandParserTest, ParsesRawProjectionQueryOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--match-id"},
      std::string_view{"12345"},
      std::string_view{"--input"},
      std::string_view{"events.json"},
      std::string_view{"--filter"},
      std::string_view{"minute=12"},
      std::string_view{"--project"},
      std::string_view{"player_name,minute"}};

  const auto options = emberdb::cli::parseOptions(arguments);
  const auto query = emberdb::cli::makeEventQuery(options);

  EXPECT_EQ(options.command, emberdb::cli::Command::Query);
  EXPECT_EQ(options.provider, "statsbomb");
  EXPECT_EQ(options.match_id, 12345);
  ASSERT_EQ(query.filters.size(), 1U);
  EXPECT_EQ(query.filters[0].column(), emberdb::FootballEventColumn::Minute);
  EXPECT_EQ(query.filters[0].value(),
            emberdb::FootballEventValue{std::int32_t{12}});
  EXPECT_EQ(query.projection,
            (std::vector{emberdb::FootballEventColumn::PlayerName,
                         emberdb::FootballEventColumn::Minute}));
}

TEST(CliCommandParserTest, ParsesGroupedAggregationOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--group-by"},
      std::string_view{"event_type"},
      std::string_view{"--aggregate"},
      std::string_view{"count(*)"},
      std::string_view{"--aggregate"},
      std::string_view{"avg(start_x)"}};

  const auto options = emberdb::cli::parseOptions(arguments);
  const auto query = emberdb::cli::makeAggregationQuery(options);

  EXPECT_EQ(options.database, "match.ember");
  EXPECT_EQ(query.group_by,
            (std::vector{emberdb::FootballEventColumn::EventType}));
  ASSERT_EQ(query.aggregates.size(), 2U);
  EXPECT_EQ(query.aggregates[0].name(), "count(*)");
  EXPECT_EQ(query.aggregates[1].name(), "avg(start_x)");
}

TEST(CliCommandParserTest, ParsesReconciliationDecisionOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"reconcile"},
      std::string_view{"reject"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--candidate-id"},
      std::string_view{"7"},
      std::string_view{"--actor"},
      std::string_view{"reviewer@example.com"},
      std::string_view{"--source"},
      std::string_view{"provider match pages"},
      std::string_view{"--reason"},
      std::string_view{"Wrong fixture"}};

  const auto options = emberdb::cli::parseOptions(arguments);

  EXPECT_EQ(options.command, emberdb::cli::Command::ReconcileReject);
  EXPECT_EQ(options.review, "review.json");
  EXPECT_EQ(options.candidate_id, 7U);
  EXPECT_EQ(options.actor, "reviewer@example.com");
  EXPECT_EQ(options.source, "provider match pages");
  EXPECT_EQ(options.reason, "Wrong fixture");
}

TEST(CliCommandParserTest, ParsesAuditedCatalogAddAndMapOptions) {
  constexpr std::array add_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"add"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"match"},
      std::string_view{"--canonical-id"},
      std::string_view{"100"},
      std::string_view{"--season-id"},
      std::string_view{"30"},
      std::string_view{"--home-team-id"},
      std::string_view{"1"},
      std::string_view{"--away-team-id"},
      std::string_view{"2"},
      std::string_view{"--home-score"},
      std::string_view{"2"},
      std::string_view{"--away-score"},
      std::string_view{"1"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"fixture"},
      std::string_view{"--reason"},
      std::string_view{"Create canonical match"}};
  const auto add = emberdb::cli::parseOptions(add_arguments);

  EXPECT_EQ(add.command, emberdb::cli::Command::CatalogAdd);
  EXPECT_EQ(add.catalog_entity, emberdb::CatalogEntityType::Match);
  EXPECT_EQ(add.canonical_id, 100);
  EXPECT_EQ(add.season_id, 30);
  EXPECT_EQ(add.home_score, 2);
  EXPECT_EQ(add.away_score, 1);

  constexpr std::array map_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"map"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"player"},
      std::string_view{"--canonical-id"},
      std::string_view{"10"},
      std::string_view{"--provider"},
      std::string_view{"Metrica"},
      std::string_view{"--provider-id"},
      std::string_view{"Player1"},
      std::string_view{"--provider-match-id"},
      std::string_view{"42"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"lineup"},
      std::string_view{"--reason"},
      std::string_view{"Verified lineup"}};
  const auto map = emberdb::cli::parseOptions(map_arguments);

  EXPECT_EQ(map.command, emberdb::cli::Command::CatalogMap);
  EXPECT_EQ(map.catalog_entity, emberdb::CatalogEntityType::Player);
  EXPECT_EQ(map.provider, "Metrica");
  EXPECT_EQ(map.provider_id, "Player1");
  EXPECT_EQ(map.provider_match_id, "42");
}

TEST(CliCommandParserTest, ParsesCatalogManifestImportOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"import"},
      std::string_view{"--manifest"},
      std::string_view{"catalog.json"},
      std::string_view{"--store"},
      std::string_view{"identities.ember-catalog"},
      std::string_view{"--dry-run"}};

  const auto options = emberdb::cli::parseOptions(arguments);

  EXPECT_EQ(options.command, emberdb::cli::Command::CatalogImport);
  EXPECT_EQ(options.manifest, "catalog.json");
  EXPECT_EQ(options.store, "identities.ember-catalog");
  EXPECT_TRUE(options.dry_run);

  constexpr std::array missing_store{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"import"},
      std::string_view{"--manifest"},
      std::string_view{"catalog.json"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(missing_store)),
      std::runtime_error);

  constexpr std::array unrelated_review{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"import"},
      std::string_view{"--manifest"},
      std::string_view{"catalog.json"},
      std::string_view{"--store"},
      std::string_view{"identities.ember-catalog"},
      std::string_view{"--review"},
      std::string_view{"review.json"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(unrelated_review)),
      std::runtime_error);
}

TEST(CliCommandParserTest, ParsesEntityCandidateLifecycleOptions) {
  constexpr std::array generate_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"candidates"},
      std::string_view{"generate"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"player"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--input"},
      std::string_view{"lineups.json"}};
  const auto generate = emberdb::cli::parseOptions(generate_arguments);

  EXPECT_EQ(generate.command,
            emberdb::cli::Command::EntityCandidateGenerate);
  EXPECT_EQ(generate.catalog_entity, emberdb::CatalogEntityType::Player);
  EXPECT_EQ(generate.provider, "statsbomb");
  EXPECT_EQ(generate.input, "lineups.json");

  constexpr std::array list_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"candidates"},
      std::string_view{"list"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"team"},
      std::string_view{"--status"},
      std::string_view{"unresolved"}};
  const auto list = emberdb::cli::parseOptions(list_arguments);

  EXPECT_EQ(list.command, emberdb::cli::Command::EntityCandidateList);
  EXPECT_EQ(list.catalog_entity, emberdb::CatalogEntityType::Team);
  EXPECT_EQ(list.candidate_status,
            emberdb::MatchCandidateStatus::Unresolved);

  constexpr std::array accept_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"candidates"},
      std::string_view{"accept"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--candidate-id"},
      std::string_view{"7"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"provider profile"},
      std::string_view{"--reason"},
      std::string_view{"Verified identity"}};
  const auto accept = emberdb::cli::parseOptions(accept_arguments);

  EXPECT_EQ(accept.command, emberdb::cli::Command::EntityCandidateAccept);
  EXPECT_EQ(accept.candidate_id, 7U);
  EXPECT_EQ(accept.actor, "reviewer");
  EXPECT_EQ(accept.source, "provider profile");
  EXPECT_EQ(accept.reason, "Verified identity");
}

TEST(CliCommandParserTest, ParsesCatalogMaintenanceOptions) {
  constexpr std::array rename_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"rename"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"player"},
      std::string_view{"--canonical-id"},
      std::string_view{"10"},
      std::string_view{"--name"},
      std::string_view{"Alex A. Forward"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"provider profile"},
      std::string_view{"--reason"},
      std::string_view{"Correct display name"}};
  const auto rename = emberdb::cli::parseOptions(rename_arguments);

  EXPECT_EQ(rename.command, emberdb::cli::Command::CatalogRename);
  EXPECT_EQ(rename.catalog_entity, emberdb::CatalogEntityType::Player);
  EXPECT_EQ(rename.canonical_id, 10);
  EXPECT_EQ(rename.name, "Alex A. Forward");

  constexpr std::array merge_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"merge"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"player"},
      std::string_view{"--canonical-id"},
      std::string_view{"11"},
      std::string_view{"--target-canonical-id"},
      std::string_view{"10"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"provider profile"},
      std::string_view{"--reason"},
      std::string_view{"Duplicate identity"}};
  const auto merge = emberdb::cli::parseOptions(merge_arguments);

  EXPECT_EQ(merge.command, emberdb::cli::Command::CatalogMerge);
  EXPECT_EQ(merge.canonical_id, 11);
  EXPECT_EQ(merge.target_canonical_id, 10);
}

TEST(CliCommandParserTest, ParsesReadOnlyCatalogValidationOptions) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"validate"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"team"},
      std::string_view{"--provider"},
      std::string_view{"wyscout"},
      std::string_view{"--input"},
      std::string_view{"teams.json"}};

  const auto options = emberdb::cli::parseOptions(arguments);

  EXPECT_EQ(options.command, emberdb::cli::Command::CatalogValidate);
  EXPECT_EQ(options.catalog_entity, emberdb::CatalogEntityType::Team);
  EXPECT_EQ(options.provider, "wyscout");
  EXPECT_EQ(options.input, "teams.json");
}

TEST(CliCommandParserTest, ParsesReadOnlyEventIdentityCoverageOptions) {
  constexpr std::array persisted_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"coverage"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--database"},
      std::string_view{"match.ember"}};
  const auto persisted = emberdb::cli::parseOptions(persisted_arguments);

  EXPECT_EQ(persisted.command, emberdb::cli::Command::CatalogCoverage);
  EXPECT_EQ(persisted.review, "review.json");
  EXPECT_EQ(persisted.database, "match.ember");

  constexpr std::array raw_arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"coverage"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--match-id"},
      std::string_view{"12345"},
      std::string_view{"--input"},
      std::string_view{"events.json"}};
  const auto raw = emberdb::cli::parseOptions(raw_arguments);

  EXPECT_EQ(raw.command, emberdb::cli::Command::CatalogCoverage);
  EXPECT_EQ(raw.provider, "statsbomb");
  EXPECT_EQ(raw.match_id, 12345);
  EXPECT_EQ(raw.input, "events.json");

  constexpr std::array mixed_sources{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"coverage"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--match-id"},
      std::string_view{"12345"},
      std::string_view{"--input"},
      std::string_view{"events.json"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(mixed_sources)),
      std::runtime_error);
}

TEST(CliCommandParserTest, ParsesTypedFiltersNullsAndQueryLimits) {
  constexpr std::array arguments{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--filter"},
      std::string_view{"minute>=12"},
      std::string_view{"--filter"},
      std::string_view{"player_name IS NOT NULL"},
      std::string_view{"--project"},
      std::string_view{"provider_event_id"},
      std::string_view{"--limit"},
      std::string_view{"5"}};

  const auto options = emberdb::cli::parseOptions(arguments);
  const auto query = emberdb::cli::makeEventQuery(options);

  ASSERT_EQ(query.filters.size(), 2U);
  EXPECT_EQ(query.filters[0].operation(),
            emberdb::FilterOperator::GreaterOrEqual);
  EXPECT_EQ(query.filters[0].value(),
            emberdb::FootballEventValue{std::int32_t{12}});
  EXPECT_EQ(query.filters[1].operation(),
            emberdb::FilterOperator::IsNotNull);
  EXPECT_FALSE(query.filters[1].operand());
  EXPECT_EQ(query.limit, 5U);
}

TEST(CliCommandParserTest, PreservesCommandValidationFailures) {
  constexpr std::array mixed_sources{
      std::string_view{"emberdb_cli"},
      std::string_view{"query"},
      std::string_view{"--database"},
      std::string_view{"match.ember"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--project"},
      std::string_view{"event_type"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(mixed_sources)),
      std::runtime_error);

  constexpr std::array missing_direction{
      std::string_view{"emberdb_cli"},
      std::string_view{"import"},
      std::string_view{"--provider"},
      std::string_view{"metrica"},
      std::string_view{"--match-id"},
      std::string_view{"42"},
      std::string_view{"--input"},
      std::string_view{"events.csv"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(missing_direction)),
      std::runtime_error);

  constexpr std::array invalid_catalog_scope{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"map"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"competition"},
      std::string_view{"--canonical-id"},
      std::string_view{"1"},
      std::string_view{"--provider"},
      std::string_view{"StatsBomb"},
      std::string_view{"--provider-id"},
      std::string_view{"2"},
      std::string_view{"--provider-match-id"},
      std::string_view{"42"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"fixture"},
      std::string_view{"--reason"},
      std::string_view{"Mapping"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(invalid_catalog_scope)),
      std::runtime_error);

  constexpr std::array invalid_entity_candidate{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"candidates"},
      std::string_view{"generate"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"match"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--input"},
      std::string_view{"matches.json"}};
  EXPECT_THROW(
      static_cast<void>(
          emberdb::cli::parseOptions(invalid_entity_candidate)),
      std::runtime_error);

  constexpr std::array invalid_self_merge{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"merge"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"player"},
      std::string_view{"--canonical-id"},
      std::string_view{"10"},
      std::string_view{"--target-canonical-id"},
      std::string_view{"10"},
      std::string_view{"--actor"},
      std::string_view{"reviewer"},
      std::string_view{"--source"},
      std::string_view{"fixture"},
      std::string_view{"--reason"},
      std::string_view{"Invalid merge"}};
  EXPECT_THROW(
      static_cast<void>(emberdb::cli::parseOptions(invalid_self_merge)),
      std::runtime_error);

  constexpr std::array invalid_catalog_validation{
      std::string_view{"emberdb_cli"},
      std::string_view{"catalog"},
      std::string_view{"validate"},
      std::string_view{"--review"},
      std::string_view{"review.json"},
      std::string_view{"--entity"},
      std::string_view{"match"},
      std::string_view{"--provider"},
      std::string_view{"statsbomb"},
      std::string_view{"--input"},
      std::string_view{"matches.json"}};
  EXPECT_THROW(
      static_cast<void>(
          emberdb::cli::parseOptions(invalid_catalog_validation)),
      std::runtime_error);
}

}  // namespace
