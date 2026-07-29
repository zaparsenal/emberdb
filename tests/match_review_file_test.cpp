#include "emberdb/persistence/match_review_file.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

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
  }

  emberdb::MatchReviewStore store() const {
    emberdb::CanonicalIdentityCatalog catalog;
    catalog.addTeam({{1}, "Arsenal"});
    catalog.addTeam({{2}, "Leicester City"});
    catalog.addPlayer({{10}, "Alex Forward"});
    catalog.addMatch({{100}, "Premier League", "2017/2018",
                      std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                      {1}, {2}, 4, 3});
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

  std::filesystem::path path_;
};

TEST_F(MatchReviewFileTest, RoundTripsCatalogEvidenceAndEveryDecisionState) {
  auto original = store();
  const auto ids = original.addCandidates(
      {candidate(original, "2499719"), candidate(original, "2499720"),
       candidate(original, "2499721")});
  original.accept(ids[0], {100});
  original.reject(ids[1], "Provider correction identifies another fixture");

  emberdb::saveMatchReviewStore(original, path_);
  const auto loaded = emberdb::loadMatchReviewStore(path_);

  EXPECT_EQ(loaded.catalog().teams().size(), 2U);
  EXPECT_EQ(loaded.catalog().players().size(), 1U);
  EXPECT_EQ(loaded.catalog().matches().size(), 1U);
  EXPECT_EQ(loaded.catalog().playerMappings().begin()->first.match_id, "match-1");
  EXPECT_EQ(loaded.catalog().resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  ASSERT_EQ(loaded.candidates().size(), 3U);
  EXPECT_EQ(loaded.candidate(ids[0])->status,
            emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(loaded.candidate(ids[1])->rejection_reason,
            "Provider correction identifies another fixture");
  EXPECT_EQ(loaded.candidate(ids[2])->status,
            emberdb::MatchCandidateStatus::Unresolved);
  EXPECT_DOUBLE_EQ(loaded.candidate(ids[2])->reconciliation.confidence, 1.0);
  EXPECT_EQ(loaded.candidate(ids[2])->reconciliation.home_team.canonical_value,
            "1");
}

TEST_F(MatchReviewFileTest, AtomicallyReplacesExistingReviewFile) {
  auto original = store();
  const auto id = original.addCandidates({candidate(original, "2499719")})[0];
  emberdb::saveMatchReviewStore(original, path_);
  original.reject(id, "Not the same match");

  emberdb::saveMatchReviewStore(original, path_);

  EXPECT_EQ(emberdb::loadMatchReviewStore(path_).candidate(id)->status,
            emberdb::MatchCandidateStatus::Rejected);
  EXPECT_FALSE(std::filesystem::exists(path_.string() + ".tmp"));
}

TEST_F(MatchReviewFileTest, RefusesStaleTemporaryFileWithoutChangingStore) {
  auto original = store();
  emberdb::saveMatchReviewStore(original, path_);
  std::ofstream(path_.string() + ".tmp") << "stale";
  original.catalog().addPlayer({{11}, "Another Player"});

  EXPECT_THROW(emberdb::saveMatchReviewStore(original, path_), std::runtime_error);
  EXPECT_EQ(emberdb::loadMatchReviewStore(path_).catalog().players().size(), 1U);
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
