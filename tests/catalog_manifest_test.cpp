#include "emberdb/identity/catalog_manifest.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "emberdb/persistence/match_review_file.h"

namespace {

constexpr auto kRecordedAt =
    std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}};

emberdb::CatalogManifestProvenance provenance(std::string reason) {
  return {"reviewer@example.com", "offline registry", std::move(reason), {}};
}

emberdb::CatalogManifestMapping mapping(std::string provider,
                                        std::string provider_id,
                                        std::string reason) {
  return {std::move(provider), std::move(provider_id), std::nullopt,
          provenance(std::move(reason)), {}, 0};
}

emberdb::CatalogManifest validManifest() {
  emberdb::CatalogManifest manifest;
  manifest.entries = {
      {emberdb::CatalogEntityType::Competition,
       20,
       "Premier League",
       0,
       0,
       0,
       0,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       provenance("Create competition"),
       {mapping("StatsBomb", "2", "Map competition")},
       {},
       0},
      {emberdb::CatalogEntityType::Season,
       30,
       "2023/2024",
       20,
       0,
       0,
       0,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       provenance("Create season"),
       {mapping("StatsBomb", "44", "Map season")},
       {},
       1},
      {emberdb::CatalogEntityType::Team,
       1,
       "North FC",
       0,
       0,
       0,
       0,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       provenance("Create home team"),
       {mapping("StatsBomb", "10", "Map home team")},
       {},
       2},
      {emberdb::CatalogEntityType::Team,
       2,
       "South FC",
       0,
       0,
       0,
       0,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       provenance("Create away team"),
       {mapping("StatsBomb", "20", "Map away team")},
       {},
       3},
      {emberdb::CatalogEntityType::Player,
       10,
       "Alex Forward",
       0,
       0,
       0,
       0,
       std::nullopt,
       std::nullopt,
       std::nullopt,
       provenance("Create player"),
       {mapping("StatsBomb", "99", "Map player")},
       {},
       4},
      {emberdb::CatalogEntityType::Match,
       100,
       "",
       0,
       30,
       1,
       2,
       1'700'000'000,
       2,
       1,
       provenance("Create match"),
       {mapping("StatsBomb", "12345", "Map match")},
       {},
       5}};
  return manifest;
}

class CatalogManifestFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    manifest_path_ = std::filesystem::temp_directory_path() /
                     ("emberdb-catalog-manifest-" + suffix + ".json");
    store_path_ = std::filesystem::temp_directory_path() /
                  ("emberdb-catalog-store-" + suffix + ".json");
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove(manifest_path_, error);
    std::filesystem::remove(store_path_, error);
    std::filesystem::remove(store_path_.string() + ".tmp", error);
    std::filesystem::remove(store_path_.string() + ".lock", error);
  }

  void writeManifest(const std::string& contents) {
    std::ofstream output(manifest_path_);
    ASSERT_TRUE(output);
    output << contents;
    ASSERT_TRUE(output);
  }

  std::filesystem::path manifest_path_;
  std::filesystem::path store_path_;
};

TEST(CatalogManifestTest, PlansValidManifestAcrossEveryEntityType) {
  const emberdb::MatchReviewStore current;

  const auto plan = emberdb::planCatalogManifestImport(
      validManifest(), current, kRecordedAt);

  EXPECT_TRUE(plan.report.importable());
  EXPECT_TRUE(plan.report.hasChanges());
  EXPECT_EQ(plan.report.summary.create, 12U);
  EXPECT_EQ(plan.report.summary.unchanged, 0U);
  EXPECT_EQ(plan.report.planned_revision, 12U);
  EXPECT_EQ(current.revision(), 0U);
  EXPECT_TRUE(current.catalog().competitions().empty());
  EXPECT_EQ(plan.resulting_store.catalog().competitions().size(), 1U);
  EXPECT_EQ(plan.resulting_store.catalog().seasons().size(), 1U);
  EXPECT_EQ(plan.resulting_store.catalog().teams().size(), 2U);
  EXPECT_EQ(plan.resulting_store.catalog().players().size(), 1U);
  EXPECT_EQ(plan.resulting_store.catalog().matches().size(), 1U);
  EXPECT_EQ(plan.resulting_store.catalog().resolveMatch(
                {"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  ASSERT_EQ(plan.resulting_store.catalogChanges().size(), 12U);
  EXPECT_EQ(plan.resulting_store.catalogChanges().front().provenance.actor,
            "reviewer@example.com");
}

TEST(CatalogManifestTest, DryRunPlanDoesNotMutateCurrentStore) {
  emberdb::MatchReviewStore current;
  current.addTeam({{50}, "Existing FC"},
                  {"reviewer", "fixture", "Existing team", kRecordedAt});
  const auto revision = current.revision();

  const auto plan = emberdb::planCatalogManifestImport(
      validManifest(), current, kRecordedAt);

  EXPECT_TRUE(plan.report.importable());
  EXPECT_EQ(current.revision(), revision);
  EXPECT_EQ(current.catalog().teams().size(), 1U);
  EXPECT_EQ(current.catalog().team({1}), nullptr);
}

TEST(CatalogManifestTest, RejectsEntirePlanWhenOneEntryIsInvalid) {
  auto manifest = validManifest();
  manifest.entries[1].competition_id = 999;
  const emberdb::MatchReviewStore current;

  const auto plan = emberdb::planCatalogManifestImport(
      manifest, current, kRecordedAt);

  EXPECT_FALSE(plan.report.importable());
  EXPECT_GT(plan.report.summary.invalid, 0U);
  EXPECT_EQ(plan.report.planned_revision, current.revision());
  EXPECT_TRUE(plan.resulting_store.catalog().competitions().empty());
  EXPECT_TRUE(plan.resulting_store.catalog().teams().empty());
}

TEST(CatalogManifestTest, ReimportIsIdempotent) {
  const auto first = emberdb::planCatalogManifestImport(
      validManifest(), emberdb::MatchReviewStore{}, kRecordedAt);
  ASSERT_TRUE(first.report.importable());
  const auto revision = first.resulting_store.revision();

  const auto second = emberdb::planCatalogManifestImport(
      validManifest(), first.resulting_store, kRecordedAt);

  EXPECT_TRUE(second.report.importable());
  EXPECT_FALSE(second.report.hasChanges());
  EXPECT_EQ(second.report.summary.unchanged, 12U);
  EXPECT_EQ(second.resulting_store.revision(), revision);
  EXPECT_EQ(second.resulting_store.catalogChanges().size(), 12U);
}

TEST(CatalogManifestTest, ReportingOrderAndResultsAreDeterministic) {
  auto reordered = validManifest();
  std::ranges::reverse(reordered.entries);

  const auto first = emberdb::planCatalogManifestImport(
      reordered, emberdb::MatchReviewStore{}, kRecordedAt);
  const auto second = emberdb::planCatalogManifestImport(
      reordered, emberdb::MatchReviewStore{}, kRecordedAt);

  ASSERT_EQ(first.report.results.size(), second.report.results.size());
  for (std::size_t index = 0; index < first.report.results.size(); ++index) {
    const auto& left = first.report.results[index];
    const auto& right = second.report.results[index];
    EXPECT_EQ(left.entity_type, right.entity_type);
    EXPECT_EQ(left.canonical_id, right.canonical_id);
    EXPECT_EQ(left.provider, right.provider);
    EXPECT_EQ(left.provider_id, right.provider_id);
    EXPECT_EQ(left.action, right.action);
    EXPECT_EQ(left.reason, right.reason);
  }
  ASSERT_FALSE(first.report.results.empty());
  EXPECT_EQ(first.report.results.front().entity_type,
            emberdb::CatalogEntityType::Competition);
  EXPECT_EQ(first.report.results.back().entity_type,
            emberdb::CatalogEntityType::Match);
}

TEST(CatalogManifestTest, DetectsDuplicateCanonicalIds) {
  auto manifest = validManifest();
  manifest.entries.push_back(manifest.entries[2]);

  const auto plan = emberdb::planCatalogManifestImport(
      manifest, emberdb::MatchReviewStore{}, kRecordedAt);

  EXPECT_FALSE(plan.report.importable());
  EXPECT_GE(plan.report.summary.invalid, 2U);
  EXPECT_TRUE(std::ranges::any_of(
      plan.report.results, [](const auto& result) {
        return result.reason.find("duplicate canonical team ID 1") !=
               std::string::npos;
      }));
}

TEST(CatalogManifestTest, DetectsProviderMappingCollisions) {
  auto manifest = validManifest();
  manifest.entries[3].mappings[0].provider_id = "10";

  const auto within_manifest = emberdb::planCatalogManifestImport(
      manifest, emberdb::MatchReviewStore{}, kRecordedAt);

  EXPECT_FALSE(within_manifest.report.importable());
  EXPECT_EQ(within_manifest.report.summary.conflicts, 2U);
  EXPECT_TRUE(std::ranges::any_of(
      within_manifest.report.results, [](const auto& result) {
        return result.reason.find("maps to multiple canonical team") !=
               std::string::npos;
      }));

  emberdb::MatchReviewStore existing;
  existing.addTeam({{7}, "Existing FC"},
                   {"reviewer", "fixture", "Create existing", kRecordedAt});
  existing.mapTeam({"StatsBomb", "10", std::nullopt}, {7},
                   {"reviewer", "fixture", "Map existing", kRecordedAt});
  const auto against_store = emberdb::planCatalogManifestImport(
      validManifest(), existing, kRecordedAt);
  EXPECT_FALSE(against_store.report.importable());
  EXPECT_TRUE(std::ranges::any_of(
      against_store.report.results, [](const auto& result) {
        return result.reason.find("already maps to canonical team 7") !=
               std::string::npos;
      }));
}

TEST(CatalogManifestTest, DetectsInvalidParentReferences) {
  auto manifest = validManifest();
  manifest.entries[1].competition_id = 999;
  manifest.entries[5].season_id = 888;
  manifest.entries[5].home_team_id = 777;

  const auto plan = emberdb::planCatalogManifestImport(
      manifest, emberdb::MatchReviewStore{}, kRecordedAt);

  EXPECT_FALSE(plan.report.importable());
  EXPECT_TRUE(std::ranges::any_of(
      plan.report.results, [](const auto& result) {
        return result.reason.find("unknown canonical competition 999") !=
               std::string::npos;
      }));
  EXPECT_TRUE(std::ranges::any_of(
      plan.report.results, [](const auto& result) {
        return result.reason.find("unknown canonical season 888") !=
               std::string::npos;
      }));
}

TEST(CatalogManifestTest, DetectsConflictingExistingMetadata) {
  emberdb::MatchReviewStore existing;
  existing.addTeam({{1}, "Different Name"},
                   {"reviewer", "fixture", "Create existing", kRecordedAt});

  const auto plan = emberdb::planCatalogManifestImport(
      validManifest(), existing, kRecordedAt);

  EXPECT_FALSE(plan.report.importable());
  EXPECT_GT(plan.report.summary.conflicts, 0U);
  EXPECT_TRUE(std::ranges::any_of(
      plan.report.results, [](const auto& result) {
        return result.canonical_id == 1 &&
               result.reason == "existing canonical team metadata differs";
      }));
}

TEST_F(CatalogManifestFileTest, RejectsUnsupportedVersion) {
  writeManifest(R"({
    "format": "emberdb-canonical-manifest",
    "version": 2,
    "teams": []
  })");

  EXPECT_THROW(
      {
        try {
          static_cast<void>(
              emberdb::loadCatalogManifest(manifest_path_));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(
                        "unsupported catalog manifest version 2"),
                    std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(CatalogManifestFileTest, ReportsMissingProvenanceAsInvalid) {
  writeManifest(R"({
    "format": "emberdb-canonical-manifest",
    "version": 1,
    "teams": [{"id": 1, "name": "North FC"}]
  })");
  const auto manifest = emberdb::loadCatalogManifest(manifest_path_);

  const auto plan = emberdb::planCatalogManifestImport(
      manifest, emberdb::MatchReviewStore{}, kRecordedAt);

  ASSERT_EQ(plan.report.results.size(), 1U);
  EXPECT_EQ(plan.report.results[0].action,
            emberdb::CatalogManifestAction::Invalid);
  EXPECT_NE(plan.report.results[0].reason.find("missing required provenance"),
            std::string::npos);
}

TEST_F(CatalogManifestFileTest, RejectsMalformedJsonWithPathContext) {
  writeManifest(R"({"format": "emberdb-canonical-manifest",)");

  EXPECT_THROW(
      {
        try {
          static_cast<void>(
              emberdb::loadCatalogManifest(manifest_path_));
        } catch (const std::runtime_error& error) {
          EXPECT_NE(std::string(error.what()).find(manifest_path_.string()),
                    std::string::npos);
          EXPECT_NE(std::string(error.what()).find("malformed JSON"),
                    std::string::npos);
          throw;
        }
      },
      std::runtime_error);
}

TEST_F(CatalogManifestFileTest, SuccessfulPlanCanAtomicallyReplaceStoreFile) {
  emberdb::MatchReviewStore initial;
  emberdb::createMatchReviewStore(initial, store_path_);
  const auto plan = emberdb::planCatalogManifestImport(
      validManifest(), initial, kRecordedAt);
  ASSERT_TRUE(plan.report.importable());

  emberdb::saveMatchReviewStore(plan.resulting_store, store_path_,
                                initial.revision());

  const auto loaded = emberdb::loadMatchReviewStore(store_path_);
  EXPECT_EQ(loaded.catalog().matches().size(), 1U);
  EXPECT_EQ(loaded.catalog().resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  EXPECT_FALSE(std::filesystem::exists(store_path_.string() + ".tmp"));
}

}  // namespace
