#include "emberdb/reconciliation/catalog_validation.h"

#include <stdexcept>

#include <gtest/gtest.h>

namespace {

TEST(CatalogValidationTest,
     ReportsMappingCoverageCollisionsAndLifecycleConflictsDeterministically) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "North FC"});
  catalog.addTeam({{3}, "Retired FC"});
  catalog.addTeam({{4}, "Solo FC"});
  catalog.addTeam({{5}, "Exact FC"});
  catalog.mapTeam({"Provider", "3", std::nullopt}, {4});
  catalog.mapTeam({"Provider", "5", std::nullopt}, {4});
  catalog.mapTeam({"Provider", "6", std::nullopt}, {3});
  catalog.deprecateTeam({3});

  emberdb::ProviderMetadata metadata;
  metadata.teams = {
      {{"Provider", "7", std::nullopt}, "Exact FC"},
      {{"Provider", "6", std::nullopt}, "Retired FC"},
      {{"Provider", "5", std::nullopt}, "Former Solo Name"},
      {{"Provider", "4", std::nullopt}, "Unknown FC"},
      {{"Provider", "3", std::nullopt}, "Solo FC"},
      {{"Provider", "2", std::nullopt}, "Retired FC"},
      {{"Provider", "1", std::nullopt}, "North FC"}};
  const auto mappings_before = catalog.teamMappings();

  const auto report = emberdb::validateCatalogMetadata(
      metadata, emberdb::IdentityEntityType::Team, catalog);

  EXPECT_EQ(catalog.teamMappings(), mappings_before);
  ASSERT_EQ(report.records.size(), 7U);
  EXPECT_EQ(report.records[0].provider_identity.id, "1");
  EXPECT_EQ(report.records[0].outcome,
            emberdb::CatalogValidationOutcome::AmbiguousExactMatch);
  EXPECT_EQ(report.records[0].canonical_ids,
            (std::vector<emberdb::Identifier>{1, 2}));
  EXPECT_EQ(report.records[1].outcome,
            emberdb::CatalogValidationOutcome::InactiveExactMatch);
  EXPECT_EQ(report.records[2].outcome,
            emberdb::CatalogValidationOutcome::MappedActive);
  EXPECT_EQ(report.records[4].name_status,
            emberdb::ReconciliationStatus::Conflicting);
  EXPECT_EQ(report.records[5].outcome,
            emberdb::CatalogValidationOutcome::MappedInactive);
  EXPECT_EQ(report.records[6].outcome,
            emberdb::CatalogValidationOutcome::ExactMatch);

  EXPECT_EQ(report.summary.provider_records, 7U);
  EXPECT_EQ(report.summary.mapped_active, 2U);
  EXPECT_EQ(report.summary.mapped_inactive, 1U);
  EXPECT_EQ(report.summary.unmapped_exact_matches, 1U);
  EXPECT_EQ(report.summary.ambiguous_exact_matches, 1U);
  EXPECT_EQ(report.summary.inactive_exact_matches, 1U);
  EXPECT_EQ(report.summary.no_exact_matches, 1U);
  EXPECT_EQ(report.summary.mapped_name_mismatches, 1U);
}

TEST(CatalogValidationTest,
     ReportsSeasonParentCoverageWithoutCreatingMappings) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "League A"});
  catalog.addCompetition({{21}, "League B"});
  catalog.addCompetition({{22}, "Old League"});
  catalog.addSeason({{30}, {20}, "2024"});
  catalog.addSeason({{31}, {21}, "2024"});
  catalog.addSeason({{32}, {22}, "Old Season"});
  catalog.addSeason({{33}, {20}, "Unique Season"});
  catalog.mapCompetition({"Provider", "a"}, {20});
  catalog.mapCompetition({"Provider", "b"}, {21});
  catalog.mapCompetition({"Provider", "old"}, {22});
  catalog.mapSeason({"Provider", "mapped"}, {30});
  catalog.deprecateSeason({32});
  catalog.deprecateCompetition({22});

  emberdb::ProviderMetadata metadata;
  metadata.seasons = {
      {{"Provider", "mapped"}, {"Provider", "b"}, "Changed Name"},
      {{"Provider", "missing-name"}, {"Provider", "a"}, std::nullopt},
      {{"Provider", "inactive"}, {"Provider", "old"}, "Old Season"},
      {{"Provider", "conflict"}, {"Provider", "b"}, "Unique Season"},
      {{"Provider", "ambiguous"}, {"Provider", "unmapped"}, "2024"},
      {{"Provider", "exact"}, {"Provider", "a"}, "2024"}};
  const auto mappings_before = catalog.seasonMappings();

  const auto report = emberdb::validateCatalogMetadata(
      metadata, emberdb::IdentityEntityType::Season, catalog);

  EXPECT_EQ(catalog.seasonMappings(), mappings_before);
  ASSERT_EQ(report.records.size(), 6U);
  EXPECT_EQ(report.records[0].provider_identity.id, "ambiguous");
  EXPECT_EQ(report.records[0].outcome,
            emberdb::CatalogValidationOutcome::AmbiguousExactMatch);
  EXPECT_EQ(report.records[0].context_status,
            emberdb::CatalogValidationContextStatus::Missing);
  EXPECT_EQ(report.records[1].outcome,
            emberdb::CatalogValidationOutcome::NoExactMatch);
  EXPECT_EQ(report.records[1].canonical_ids,
            (std::vector<emberdb::Identifier>{33}));
  EXPECT_EQ(report.records[1].context_status,
            emberdb::CatalogValidationContextStatus::Conflicting);
  EXPECT_EQ(report.records[2].outcome,
            emberdb::CatalogValidationOutcome::ExactMatch);
  EXPECT_EQ(report.records[3].outcome,
            emberdb::CatalogValidationOutcome::InactiveExactMatch);
  EXPECT_EQ(report.records[3].context_status,
            emberdb::CatalogValidationContextStatus::Inactive);
  EXPECT_EQ(report.records[4].outcome,
            emberdb::CatalogValidationOutcome::MappedActive);
  EXPECT_EQ(report.records[4].name_status,
            emberdb::ReconciliationStatus::Conflicting);
  EXPECT_EQ(report.records[4].context_status,
            emberdb::CatalogValidationContextStatus::Conflicting);
  EXPECT_EQ(report.records[5].outcome,
            emberdb::CatalogValidationOutcome::MissingName);

  EXPECT_EQ(report.summary.parent_context_missing, 1U);
  EXPECT_EQ(report.summary.parent_context_conflicts, 2U);
  EXPECT_EQ(report.summary.parent_context_inactive, 1U);
  EXPECT_EQ(report.summary.missing_names, 1U);
  EXPECT_EQ(report.summary.mapped_name_mismatches, 1U);
}

TEST(CatalogValidationTest, RejectsConflictingDuplicateProviderRecords) {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "League A"});
  catalog.addSeason({{30}, {20}, "2024"});
  emberdb::ProviderMetadata metadata;
  metadata.seasons = {
      {{"Provider", "season"}, {"Provider", "a"}, "2024"},
      {{"Provider", "season"}, {"Provider", "b"}, "2024"}};

  EXPECT_THROW(
      static_cast<void>(emberdb::validateCatalogMetadata(
          metadata, emberdb::IdentityEntityType::Season, catalog)),
      std::invalid_argument);
}

}  // namespace
