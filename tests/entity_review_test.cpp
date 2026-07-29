#include "emberdb/reconciliation/match_review.h"

#include <chrono>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>

namespace {

emberdb::ReviewProvenance provenance(std::string reason) {
  return {"reviewer", "unit-test", std::move(reason),
          std::chrono::sys_seconds{std::chrono::seconds{1'700'000'000}}};
}

emberdb::MatchReviewStore store() {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addCompetition({{21}, "Other League"});
  catalog.addSeason({{30}, {20}, "2023/2024"});
  catalog.addTeam({{1}, "North FC"});
  catalog.addTeam({{2}, "Other FC"});
  catalog.addPlayer({{10}, "Alex Forward"});
  return emberdb::MatchReviewStore(std::move(catalog));
}

emberdb::EntityReconciliation candidate(
    emberdb::IdentityEntityType type, std::string provider_id,
    emberdb::Identifier canonical_id,
    std::optional<std::string> match_id = std::nullopt) {
  std::string name;
  switch (type) {
    case emberdb::IdentityEntityType::Competition:
      name = "Premier League";
      break;
    case emberdb::IdentityEntityType::Season:
      name = "2023/2024";
      break;
    case emberdb::IdentityEntityType::Team:
      name = "North FC";
      break;
    case emberdb::IdentityEntityType::Player:
      name = "Alex Forward";
      break;
  }
  return {type,
          {"Provider", std::move(provider_id), std::move(match_id)},
          canonical_id,
          "provider metadata fixture",
          {emberdb::ReconciliationStatus::Agreeing, name, name},
          {emberdb::ReconciliationStatus::Missing, std::nullopt,
           std::nullopt},
          1.0};
}

TEST(EntityReviewStoreTest,
     AddsOnceFiltersAndAcceptsEveryEntityMappingWithAudit) {
  auto review = store();
  const std::vector candidates{
      candidate(emberdb::IdentityEntityType::Competition, "competition", 20),
      candidate(emberdb::IdentityEntityType::Season, "season", 30),
      candidate(emberdb::IdentityEntityType::Team, "team", 1),
      candidate(emberdb::IdentityEntityType::Player, "player", 10, "42")};
  const auto ids = review.addEntityCandidates(candidates);
  const auto repeated = review.addEntityCandidates(candidates);

  EXPECT_EQ(ids, repeated);
  EXPECT_EQ(review.revision(), 1U);
  EXPECT_EQ(review.entityCandidates().size(), 4U);
  EXPECT_EQ(review.entityCandidates(
                emberdb::MatchCandidateStatus::Unresolved,
                emberdb::IdentityEntityType::Team)
                .size(),
            1U);

  for (const auto id : ids) {
    review.acceptEntityCandidate(id, provenance("Verified identity"));
  }

  EXPECT_EQ(review.revision(), 5U);
  EXPECT_EQ(review.catalogChanges().size(), 4U);
  EXPECT_EQ(review.catalog().resolveCompetition({"Provider", "competition"}),
            emberdb::CanonicalCompetitionId{20});
  EXPECT_EQ(review.catalog().resolveSeason({"Provider", "season"}),
            emberdb::CanonicalSeasonId{30});
  EXPECT_EQ(review.catalog().resolveTeam(
                {"Provider", "team", std::nullopt}),
            emberdb::CanonicalTeamId{1});
  EXPECT_EQ(review.catalog().resolvePlayer({"Provider", "player", "42"}),
            emberdb::CanonicalPlayerId{10});
  EXPECT_EQ(review.entityCandidate(ids.back())->decision_provenance->actor,
            "reviewer");
}

TEST(EntityReviewStoreTest, RejectsIdempotentlyWithoutCreatingMapping) {
  auto review = store();
  const auto id =
      review
          .addEntityCandidates({candidate(
              emberdb::IdentityEntityType::Team, "team", 1)})[0];

  review.rejectEntityCandidate(id, provenance("Different organization"));
  review.rejectEntityCandidate(id, provenance("Different organization"));

  EXPECT_EQ(review.entityCandidate(id)->status,
            emberdb::MatchCandidateStatus::Rejected);
  EXPECT_EQ(review.entityCandidate(id)->rejection_reason,
            "Different organization");
  EXPECT_FALSE(
      review.catalog().resolveTeam({"Provider", "team", std::nullopt}));
  EXPECT_THROW(
      review.acceptEntityCandidate(id, provenance("Changed my mind")),
      std::invalid_argument);
}

TEST(EntityReviewStoreTest,
     LeavesCandidateUnresolvedWhenMappingConflictsAtAcceptance) {
  auto review = store();
  review.mapTeam({"Provider", "team", std::nullopt}, {2},
                 provenance("Existing mapping"));
  const auto id =
      review
          .addEntityCandidates({candidate(
              emberdb::IdentityEntityType::Team, "team", 1)})[0];

  EXPECT_THROW(
      review.acceptEntityCandidate(id, provenance("Conflicting decision")),
      std::invalid_argument);

  EXPECT_EQ(review.entityCandidate(id)->status,
            emberdb::MatchCandidateStatus::Unresolved);
  EXPECT_EQ(review.catalog().resolveTeam(
                {"Provider", "team", std::nullopt}),
            emberdb::CanonicalTeamId{2});
}

TEST(EntityReviewStoreTest, RejectsInvalidPersistedEntityCandidates) {
  auto review = store();
  auto reconciliation =
      candidate(emberdb::IdentityEntityType::Player, "player", 10);
  reconciliation.confidence = 2.0;
  emberdb::EntityCandidateRecord record{
      1, reconciliation, emberdb::MatchCandidateStatus::Unresolved,
      std::nullopt, std::nullopt};

  EXPECT_THROW(
      static_cast<void>(emberdb::MatchReviewStore::restore(
          review.catalog(), {}, 0, {}, {record})),
      std::invalid_argument);
}

}  // namespace
