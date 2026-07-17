#include "emberdb/reconciliation/match_review.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

namespace {

emberdb::MatchReviewStore reviewStore() {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addTeam({{1}, "Arsenal"});
  catalog.addTeam({{2}, "Leicester City"});
  catalog.addMatch({{100}, "Premier League", "2017/2018",
                    std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                    {1}, {2}, 4, 3});
  catalog.addMatch({{101}, "Premier League", "2017/2018",
                    std::chrono::sys_seconds{std::chrono::seconds{1'500'086'400}},
                    {1}, {2}, 1, 0});
  catalog.mapTeam({"StatsBomb", "10", std::nullopt}, {1});
  catalog.mapTeam({"StatsBomb", "20", std::nullopt}, {2});
  catalog.mapTeam({"Wyscout", "1609", std::nullopt}, {1});
  catalog.mapTeam({"Wyscout", "1631", std::nullopt}, {2});
  return emberdb::MatchReviewStore(std::move(catalog));
}

emberdb::ProviderMatchMetadata providerMatch(std::string provider, std::string id,
                                             std::string home, std::string away) {
  return {{provider, id},
          "league",
          "Premier League",
          "season",
          "2017/2018",
          std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
          {provider, home, std::nullopt},
          {provider, away, std::nullopt},
          4,
          3};
}

emberdb::MatchReconciliation candidate(const emberdb::MatchReviewStore& store) {
  return emberdb::reconcileMatches(
      providerMatch("StatsBomb", "12345", "10", "20"),
      providerMatch("Wyscout", "2499719", "1609", "1631"), store.catalog());
}

TEST(MatchReviewStoreTest, AddsCandidatesOnceAndListsByStatus) {
  auto store = reviewStore();
  const auto reconciliation = candidate(store);
  const auto first = store.addCandidates({reconciliation});
  const auto repeated = store.addCandidates({reconciliation});

  ASSERT_EQ(first.size(), 1U);
  EXPECT_EQ(first, repeated);
  EXPECT_EQ(store.candidates().size(), 1U);
  EXPECT_EQ(store.candidates(emberdb::MatchCandidateStatus::Unresolved).size(), 1U);
  EXPECT_TRUE(store.candidates(emberdb::MatchCandidateStatus::Accepted).empty());
  ASSERT_NE(store.candidate(first[0]), nullptr);
  EXPECT_DOUBLE_EQ(store.candidate(first[0])->reconciliation.confidence, 1.0);
}

TEST(MatchReviewStoreTest, AcceptsIdempotentlyAndCreatesBothMappings) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];

  store.accept(id, {100});
  store.accept(id, {100});

  ASSERT_NE(store.candidate(id), nullptr);
  EXPECT_EQ(store.candidate(id)->status, emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(store.candidate(id)->accepted_match_id, emberdb::CanonicalMatchId{100});
  EXPECT_EQ(store.catalog().resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(store.catalog().resolveMatch({"Wyscout", "2499719"}),
            emberdb::CanonicalMatchId{100});
}

TEST(MatchReviewStoreTest, RejectsIdempotentlyAndPreservesReason) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];

  store.reject(id, "Broadcast date proves these are different fixtures");
  store.reject(id, "Broadcast date proves these are different fixtures");

  EXPECT_EQ(store.candidate(id)->status, emberdb::MatchCandidateStatus::Rejected);
  EXPECT_EQ(store.candidate(id)->rejection_reason,
            "Broadcast date proves these are different fixtures");
  EXPECT_TRUE(store.catalog().matchMappings().empty());
}

TEST(MatchReviewStoreTest, RejectsConflictingFinalDecisions) {
  auto accepted = reviewStore();
  const auto accepted_id = accepted.addCandidates({candidate(accepted)})[0];
  accepted.accept(accepted_id, {100});
  EXPECT_THROW(accepted.reject(accepted_id, "Changed my mind"),
               std::invalid_argument);
  EXPECT_THROW(accepted.accept(accepted_id, {101}), std::invalid_argument);

  auto rejected = reviewStore();
  const auto rejected_id = rejected.addCandidates({candidate(rejected)})[0];
  rejected.reject(rejected_id, "Wrong fixture");
  EXPECT_THROW(rejected.accept(rejected_id, {100}), std::invalid_argument);
  EXPECT_THROW(rejected.reject(rejected_id, "Different reason"),
               std::invalid_argument);
}

TEST(MatchReviewStoreTest, ValidatesBeforeCreatingAnyAcceptedMapping) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];
  store.catalog().mapMatch({"Wyscout", "2499719"}, {101});
  EXPECT_THROW(store.accept(id, {100}), std::invalid_argument);
  EXPECT_FALSE(store.catalog().resolveMatch({"StatsBomb", "12345"}));
  EXPECT_EQ(store.candidate(id)->status,
            emberdb::MatchCandidateStatus::Unresolved);
}

TEST(MatchReviewStoreTest, RejectsBlankReasonsAndDisqualifiedComparisons) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];
  EXPECT_THROW(store.reject(id, " \t"), std::invalid_argument);
  auto comparison = candidate(store);
  comparison.is_candidate = false;
  EXPECT_THROW(static_cast<void>(store.addCandidates({comparison})),
               std::invalid_argument);
}

}  // namespace
