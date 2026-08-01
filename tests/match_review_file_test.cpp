#include "emberdb/persistence/match_review_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path fixturePath(const std::string& name) {
  return std::filesystem::path(EMBERDB_TEST_FIXTURES_DIR) / name;
}

void writeUniquelyResolvableLegacyStore(const std::filesystem::path& path,
                                        std::uint32_t version) {
  std::ofstream output(path);
  ASSERT_TRUE(output);
  const auto lifecycle = version >= 4
                             ? R"(, "status": "active", "merged_into": null)"
                             : "";
  output << R"({
    "format": "emberdb-match-review",
    "version": )"
         << version << R"(,
    "revision": 0,
    "catalog": {
      "competitions": [{"id": 20, "name": "Premier League")"
         << lifecycle << R"(}],
      "seasons": [{
        "id": 30,
        "competition_id": 20,
        "name": "2017/2018")"
         << lifecycle << R"(
      }],
      "teams": [
        {"id": 1, "name": "North FC")"
         << lifecycle << R"(},
        {"id": 2, "name": "South FC")"
         << lifecycle << R"(}
      ],
      "players": [],
      "matches": [{
        "id": 100,
        "competition": "Premier League",
        "season": "2017/2018",
        "kickoff_seconds": null,
        "home_team_id": 1,
        "away_team_id": 2,
        "home_score": null,
        "away_score": null
      }],
      "competition_mappings": [],
      "season_mappings": [],
      "team_mappings": [],
      "player_mappings": [],
      "match_mappings": []
    },
    "catalog_changes": [],)";
  if (version >= 3) {
    output << R"(
    "entity_candidates": [],)";
  }
  output << R"(
    "candidates": []
  })";
  ASSERT_TRUE(output);
}

class MatchReviewFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("emberdb-match-review-" +
             std::to_string(std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count()) +
             ".json");
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove(path_, error);
    std::filesystem::remove(path_.string() + ".tmp", error);
    std::filesystem::remove(path_.string() + ".lock", error);
  }

  emberdb::ReviewProvenance provenance(std::string reason) const {
    return {"reviewer", "unit-test", std::move(reason),
            std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
  }

  emberdb::MatchReviewStore store() const {
    emberdb::CanonicalIdentityCatalog catalog;
    catalog.addCompetition({{20}, "Premier League"});
    catalog.addSeason({{30}, {20}, "2017/2018"});
    catalog.addTeam({{1}, "Arsenal"});
    catalog.addTeam({{2}, "Leicester City"});
    catalog.addPlayer({{10}, "Alex Forward"});
    catalog.addMatch({{100}, {30},
                      std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                      {1}, {2}, 4, 3});
    catalog.mapCompetition({"StatsBomb", "2"}, {20});
    catalog.mapCompetition({"Wyscout", "364"}, {20});
    catalog.mapSeason({"StatsBomb", "44"}, {30});
    catalog.mapSeason({"Wyscout", "181150"}, {30});
    catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
    catalog.mapTeam({"StatsBomb", "20", std::nullopt}, {2});
    catalog.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
    catalog.mapTeam({"Wyscout", "1631", std::nullopt}, {2});
    catalog.mapPlayer({"Metrica", "Player1", "match-1"}, {10});
    return emberdb::MatchReviewStore(std::move(catalog));
  }

  emberdb::MatchReconciliation candidate(
      const emberdb::MatchReviewStore& store, std::string right_id) const {
    const emberdb::ProviderMatchMetadata left{
        {"StatsBomb", "12345"}, "league", "Premier League", "season",
        "2017/2018",
        std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
        {"StatsBomb", "10", std::nullopt}, {"StatsBomb", "20", std::nullopt},
        4, 3};
    const emberdb::ProviderMatchMetadata right{
        {"Wyscout", std::move(right_id)}, "league", "Premier League", "season",
        "2017/2018",
        std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
        {"Wyscout", "1609", std::nullopt}, {"Wyscout", "1631", std::nullopt},
        4, 3};
    return emberdb::reconcileMatches(left, right, store.catalog());
  }

  emberdb::EntityReconciliation entityCandidate(
      emberdb::IdentityEntityType entity_type, std::string provider_id,
      emberdb::Identifier canonical_id, std::string name) const {
    return {entity_type,
            {"OtherProvider", std::move(provider_id), std::nullopt},
            canonical_id,
            "entity metadata fixture",
            {emberdb::ReconciliationStatus::Agreeing, name, name},
            {emberdb::ReconciliationStatus::Missing, std::nullopt,
             std::nullopt},
            1.0};
  }

  std::filesystem::path path_;
};

TEST_F(MatchReviewFileTest, RoundTripsCatalogEvidenceAndEveryDecisionState) {
  auto original = store();
  original.addPlayer({{11}, "A. Forward"},
                     provenance("Create duplicate player"));
  const auto ids = original.addCandidates(
      {candidate(original, "2499719"), candidate(original, "2499720"),
       candidate(original, "2499721")});
  const auto entity_ids = original.addEntityCandidates(
      {entityCandidate(emberdb::IdentityEntityType::Competition,
                       "competition", 20, "Premier League"),
       entityCandidate(emberdb::IdentityEntityType::Team, "team", 1,
                       "Arsenal"),
       entityCandidate(emberdb::IdentityEntityType::Player, "player", 10,
                       "Alex Forward"),
       entityCandidate(emberdb::IdentityEntityType::Player,
                       "duplicate-player", 11, "A. Forward")});
  original.accept(ids[0], {100}, provenance("Metadata agrees"));
  original.reject(
      ids[1],
      provenance("Provider correction identifies another fixture"));
  original.acceptEntityCandidate(entity_ids[0],
                                 provenance("Competition verified"));
  original.rejectEntityCandidate(entity_ids[1],
                                 provenance("Different team"));
  original.acceptEntityCandidate(entity_ids[3],
                                 provenance("Duplicate player verified"));
  original.mergeCatalogEntity(emberdb::CatalogEntityType::Player, 11, 10,
                              provenance("Merge duplicate player"));
  original.renameCatalogEntity(emberdb::CatalogEntityType::Player, 10,
                               "Alex A. Forward",
                               provenance("Correct player name"));
  original.deprecateCatalogEntity(emberdb::CatalogEntityType::Team, 2,
                                  provenance("Retire duplicate team"));
  original.deprecateCatalogEntity(emberdb::CatalogEntityType::Season, 30,
                                  provenance("Retire completed season"));
  original.deprecateCatalogEntity(emberdb::CatalogEntityType::Competition, 20,
                                  provenance("Retire competition"));

  emberdb::createMatchReviewStore(original, path_);
  const auto loaded = emberdb::loadMatchReviewStore(path_);

  EXPECT_EQ(loaded.catalog().competitions().size(), 1U);
  EXPECT_EQ(loaded.catalog().seasons().size(), 1U);
  EXPECT_EQ(loaded.catalog().teams().size(), 2U);
  EXPECT_EQ(loaded.catalog().players().size(), 2U);
  EXPECT_EQ(loaded.catalog().matches().size(), 1U);
  EXPECT_EQ(loaded.catalog().match({100})->season_id,
            emberdb::CanonicalSeasonId{30});
  EXPECT_FALSE(loaded.catalog().match({100})->legacy_ancestry);
  EXPECT_EQ(loaded.catalog().playerMappings().begin()->first.match_id, "match-1");
  EXPECT_EQ(loaded.catalog().resolveCompetition({"Wyscout", "364"}),
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(loaded.catalog().resolveSeason({"StatsBomb", "44"}),
            emberdb::CanonicalSeasonId{30});
  EXPECT_EQ(loaded.catalog().resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  ASSERT_EQ(loaded.candidates().size(), 3U);
  EXPECT_EQ(loaded.candidate(ids[0])->status,
            emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(loaded.candidate(ids[1])->rejection_reason,
            "Provider correction identifies another fixture");
  EXPECT_EQ(loaded.candidate(ids[0])->decision_provenance->source,
            "unit-test");
  EXPECT_EQ(loaded.candidate(ids[2])->status,
            emberdb::MatchCandidateStatus::Unresolved);
  EXPECT_DOUBLE_EQ(loaded.candidate(ids[2])->reconciliation.confidence, 1.0);
  EXPECT_EQ(loaded.candidate(ids[2])->reconciliation.home_team.canonical_value,
            "1");
  ASSERT_EQ(loaded.entityCandidates().size(), 4U);
  EXPECT_EQ(loaded.entityCandidate(entity_ids[0])->status,
            emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(loaded.catalog().resolveCompetition(
                {"OtherProvider", "competition"}),
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(loaded.entityCandidate(entity_ids[1])->rejection_reason,
            "Different team");
  EXPECT_EQ(loaded.entityCandidate(entity_ids[2])->status,
            emberdb::MatchCandidateStatus::Unresolved);
  EXPECT_EQ(loaded.catalog().player({10})->name, "Alex A. Forward");
  EXPECT_EQ(loaded.catalog().player({11})->status,
            emberdb::CanonicalEntityStatus::Merged);
  EXPECT_EQ(loaded.catalog().player({11})->merged_into,
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(loaded.catalog().team({2})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  EXPECT_EQ(loaded.catalog().season({30})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  EXPECT_EQ(loaded.catalog().competition({20})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  EXPECT_EQ(loaded.entityCandidate(entity_ids[3])->status,
            emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(loaded.catalog().resolvePlayer(
                {"OtherProvider", "duplicate-player", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  ASSERT_EQ(loaded.catalogChanges().size(), 8U);
  EXPECT_EQ(loaded.catalogChanges()[3].action,
            emberdb::CatalogChangeAction::Merge);
  EXPECT_EQ(loaded.catalogChanges()[3].related_canonical_id, 10);
}

TEST_F(MatchReviewFileTest, AtomicallyReplacesExistingReviewFile) {
  auto original = store();
  const auto id = original.addCandidates({candidate(original, "2499719")})[0];
  emberdb::createMatchReviewStore(original, path_);
  const auto persisted_revision = original.revision();
  original.reject(id, provenance("Not the same match"));

  emberdb::saveMatchReviewStore(original, path_, persisted_revision);

  EXPECT_EQ(emberdb::loadMatchReviewStore(path_).candidate(id)->status,
            emberdb::MatchCandidateStatus::Rejected);
  EXPECT_FALSE(std::filesystem::exists(path_.string() + ".tmp"));
}

TEST_F(MatchReviewFileTest, RefusesStaleTemporaryFileWithoutChangingStore) {
  auto original = store();
  emberdb::createMatchReviewStore(original, path_);
  std::ofstream(path_.string() + ".tmp") << "stale";
  const auto persisted_revision = original.revision();
  original.addPlayer({{11}, "Another Player"},
                     provenance("Add roster member"));

  EXPECT_THROW(
      emberdb::saveMatchReviewStore(original, path_, persisted_revision),
      std::runtime_error);
  EXPECT_EQ(emberdb::loadMatchReviewStore(path_).catalog().players().size(), 1U);
}

TEST_F(MatchReviewFileTest, RejectsStaleRevisionWithoutOverwritingNewerStore) {
  auto original = store();
  emberdb::createMatchReviewStore(original, path_);
  auto first = emberdb::loadMatchReviewStore(path_);
  auto stale = emberdb::loadMatchReviewStore(path_);
  const auto shared_revision = first.revision();
  first.addPlayer({{11}, "First Player"}, provenance("First edit"));
  stale.addPlayer({{12}, "Stale Player"}, provenance("Stale edit"));

  emberdb::saveMatchReviewStore(first, path_, shared_revision);
  EXPECT_THROW(
      emberdb::saveMatchReviewStore(stale, path_, shared_revision),
      std::runtime_error);

  const auto loaded = emberdb::loadMatchReviewStore(path_);
  EXPECT_NE(loaded.catalog().player({11}), nullptr);
  EXPECT_EQ(loaded.catalog().player({12}), nullptr);
  ASSERT_EQ(loaded.catalogChanges().size(), 1U);
  EXPECT_EQ(loaded.catalogChanges()[0].canonical_name, "First Player");
  EXPECT_EQ(loaded.catalogChanges()[0].provenance.reason, "First edit");
}

TEST_F(MatchReviewFileTest, LoadsVersionThreeEntitiesAsActive) {
  std::ofstream(path_) << R"({
    "format": "emberdb-match-review",
    "version": 3,
    "revision": 0,
    "catalog": {
      "competitions": [{"id": 20, "name": "Premier League"}],
      "seasons": [
        {"id": 30, "competition_id": 20, "name": "2023/2024"}
      ],
      "teams": [
        {"id": 1, "name": "North FC"},
        {"id": 2, "name": "South FC"}
      ],
      "players": [{"id": 10, "name": "Alex Forward"}],
      "matches": [],
      "competition_mappings": [],
      "season_mappings": [],
      "team_mappings": [],
      "player_mappings": [],
      "match_mappings": []
    },
    "catalog_changes": [],
    "entity_candidates": [],
    "candidates": []
  })";

  const auto loaded = emberdb::loadMatchReviewStore(path_);

  EXPECT_EQ(loaded.catalog().competition({20})->status,
            emberdb::CanonicalEntityStatus::Active);
  EXPECT_EQ(loaded.catalog().season({30})->status,
            emberdb::CanonicalEntityStatus::Active);
  EXPECT_EQ(loaded.catalog().team({1})->status,
            emberdb::CanonicalEntityStatus::Active);
  EXPECT_EQ(loaded.catalog().player({10})->status,
            emberdb::CanonicalEntityStatus::Active);
}

TEST_F(MatchReviewFileTest, MigratesUniqueLegacyMatchAncestryToTypedSeason) {
  std::ofstream(path_) << R"({
    "format": "emberdb-match-review",
    "version": 3,
    "revision": 0,
    "catalog": {
      "competitions": [{"id": 20, "name": "Premier League"}],
      "seasons": [
        {"id": 30, "competition_id": 20, "name": "2017/2018"}
      ],
      "teams": [
        {"id": 1, "name": "North FC"},
        {"id": 2, "name": "South FC"}
      ],
      "players": [],
      "matches": [{
        "id": 100,
        "competition": "Premier League",
        "season": "2017/2018",
        "kickoff_seconds": null,
        "home_team_id": 1,
        "away_team_id": 2,
        "home_score": null,
        "away_score": null
      }],
      "competition_mappings": [],
      "season_mappings": [],
      "team_mappings": [],
      "player_mappings": [],
      "match_mappings": []
    },
    "catalog_changes": [],
    "entity_candidates": [],
    "candidates": []
  })";

  const auto loaded = emberdb::loadMatchReviewStore(path_);

  ASSERT_NE(loaded.catalog().match({100}), nullptr);
  EXPECT_EQ(loaded.catalog().match({100})->season_id,
            emberdb::CanonicalSeasonId{30});
  EXPECT_FALSE(loaded.catalog().match({100})->legacy_ancestry);
}

TEST_F(MatchReviewFileTest,
       MigratesUniqueAncestryAcrossLegacyVersionsTwoThroughFour) {
  for (const auto version : {2U, 3U, 4U}) {
    SCOPED_TRACE(version);
    writeUniquelyResolvableLegacyStore(path_, version);

    const auto loaded = emberdb::loadMatchReviewStore(path_);

    ASSERT_NE(loaded.catalog().match({100}), nullptr);
    EXPECT_EQ(loaded.catalog().match({100})->season_id,
              emberdb::CanonicalSeasonId{30});
    EXPECT_FALSE(loaded.catalog().match({100})->legacy_ancestry);
  }
}

TEST_F(MatchReviewFileTest,
       RetainsAmbiguousLegacyMatchAncestryWithoutGuessing) {
  std::ofstream(path_) << R"({
    "format": "emberdb-match-review",
    "version": 3,
    "revision": 0,
    "catalog": {
      "competitions": [
        {"id": 20, "name": "Premier League"},
        {"id": 21, "name": "Premier League"}
      ],
      "seasons": [
        {"id": 30, "competition_id": 20, "name": "2017/2018"},
        {"id": 31, "competition_id": 21, "name": "2017/2018"}
      ],
      "teams": [
        {"id": 1, "name": "North FC"},
        {"id": 2, "name": "South FC"}
      ],
      "players": [],
      "matches": [{
        "id": 100,
        "competition": "Premier League",
        "season": "2017/2018",
        "kickoff_seconds": null,
        "home_team_id": 1,
        "away_team_id": 2,
        "home_score": null,
        "away_score": null
      }],
      "competition_mappings": [],
      "season_mappings": [],
      "team_mappings": [],
      "player_mappings": [],
      "match_mappings": []
    },
    "catalog_changes": [],
    "entity_candidates": [],
    "candidates": []
  })";

  const auto loaded = emberdb::loadMatchReviewStore(path_);

  ASSERT_NE(loaded.catalog().match({100}), nullptr);
  EXPECT_FALSE(loaded.catalog().match({100})->season_id);
  ASSERT_TRUE(loaded.catalog().match({100})->legacy_ancestry);
  EXPECT_EQ(loaded.catalog().match({100})->legacy_ancestry->competition,
            "Premier League");
  EXPECT_EQ(loaded.catalog().match({100})->legacy_ancestry->season,
            "2017/2018");
}

TEST_F(MatchReviewFileTest,
       RoundTripsVersionOneLegacyAncestryExplicitlyInVersionFive) {
  const auto legacy = emberdb::loadMatchReviewStore(
      fixturePath("match_review_store.json"));
  ASSERT_NE(legacy.catalog().match({100}), nullptr);
  EXPECT_FALSE(legacy.catalog().match({100})->season_id);
  ASSERT_TRUE(legacy.catalog().match({100})->legacy_ancestry);

  emberdb::createMatchReviewStore(legacy, path_);
  const auto round_tripped = emberdb::loadMatchReviewStore(path_);

  EXPECT_FALSE(round_tripped.catalog().match({100})->season_id);
  ASSERT_TRUE(round_tripped.catalog().match({100})->legacy_ancestry);
  EXPECT_EQ(round_tripped.catalog().match({100})->legacy_ancestry->competition,
            "Premier League");
  EXPECT_EQ(round_tripped.catalog().match({100})->legacy_ancestry->season,
            "2017/2018");
}

TEST_F(MatchReviewFileTest, RejectsUnsupportedAndMalformedFilesWithContext) {
  std::ofstream(path_) << R"({"format":"emberdb-match-review","version":99})";
  try {
    static_cast<void>(emberdb::loadMatchReviewStore(path_));
    FAIL() << "expected unsupported version to fail";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find(path_.string()), std::string::npos);
    EXPECT_NE(std::string(error.what()).find("version 99"), std::string::npos);
  }
  std::ofstream(path_, std::ios::trunc) << "not json";
  EXPECT_THROW(static_cast<void>(emberdb::loadMatchReviewStore(path_)),
               std::runtime_error);
}

}  // namespace
