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

emberdb::MatchReviewStore reviewStore() {
  emberdb::CanonicalIdentityCatalog catalog;
  catalog.addCompetition({{20}, "Premier League"});
  catalog.addSeason({{30}, {20}, "2017/2018"});
  catalog.addTeam({{1}, "Arsenal"});
  catalog.addTeam({{2}, "Leicester City"});
  catalog.addMatch({{100}, {30},
                    std::chrono::sys_seconds{std::chrono::seconds{1'500'000'000}},
                    {1}, {2}, 4, 3});
  catalog.addMatch({{101}, {30},
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
  EXPECT_EQ(store.revision(), 1U);
}

TEST(MatchReviewStoreTest, AuditsCatalogAuthoringAndMappingRevisions) {
  emberdb::MatchReviewStore store;
  store.addCompetition({{20}, "Premier League"},
                       provenance("Create competition"));
  store.addSeason({{30}, {20}, "2023/2024"},
                  provenance("Create season"));
  store.addTeam({{1}, "North FC"}, provenance("Create team"));
  store.addPlayer({{10}, "Alex Forward"}, provenance("Create player"));
  store.mapCompetition({"StatsBomb", "2"}, {20},
                       provenance("Map provider competition"));
  store.mapSeason({"StatsBomb", "44"}, {30},
                  provenance("Map provider season"));
  store.mapTeam({"StatsBomb", "10", std::nullopt}, {1},
                provenance("Map provider team"));
  store.mapPlayer({"Metrica", "Player1", "42"}, {10},
                  provenance("Map match-local player"));
  store.addTeam({{2}, "South FC"}, provenance("Create away team"));
  store.addMatch({{100}, {30}, std::nullopt,
                  {1}, {2}, std::nullopt, std::nullopt},
                 provenance("Create match"));
  store.mapMatch({"StatsBomb", "12345"}, {100},
                 provenance("Map provider match"));
  const auto revision = store.revision();
  store.mapPlayer({"Metrica", "Player1", "42"}, {10},
                  provenance("Repeat mapping"));
  store.mapMatch({"StatsBomb", "12345"}, {100},
                 provenance("Repeat match mapping"));

  EXPECT_EQ(revision, 11U);
  EXPECT_EQ(store.revision(), revision);
  ASSERT_EQ(store.catalogChanges().size(), 11U);
  EXPECT_EQ(store.catalogChanges().front().entity_type,
            emberdb::CatalogEntityType::Competition);
  EXPECT_EQ(store.catalogChanges().back().entity_type,
            emberdb::CatalogEntityType::Match);
  EXPECT_EQ(store.catalogChanges().back().provider_id, "12345");
  EXPECT_EQ(store.catalogChanges().back().provenance.actor, "reviewer");
}

TEST(MatchReviewStoreTest,
     AuditsRenameMergeAndDeprecationIdempotently) {
  auto store = reviewStore();
  store.addPlayer({{10}, "Alex Forward"},
                  provenance("Create canonical player"));
  store.addPlayer({{11}, "A. Forward"},
                  provenance("Create duplicate player"));
  store.mapPlayer({"StatsBomb", "199", std::nullopt}, {11},
                  provenance("Map duplicate player"));
  store.renameCatalogEntity(emberdb::CatalogEntityType::Team, 1,
                            "Arsenal Women",
                            provenance("Correct team name"));
  store.mergeCatalogEntity(emberdb::CatalogEntityType::Player, 11, 10,
                           provenance("Merge duplicate player"));
  store.deprecateCatalogEntity(emberdb::CatalogEntityType::Team, 2,
                               provenance("Team record retired"));
  const auto revision = store.revision();

  store.renameCatalogEntity(emberdb::CatalogEntityType::Team, 1,
                            "Arsenal Women",
                            provenance("Repeat rename"));
  store.mergeCatalogEntity(emberdb::CatalogEntityType::Player, 11, 10,
                           provenance("Repeat merge"));
  store.deprecateCatalogEntity(emberdb::CatalogEntityType::Team, 2,
                               provenance("Repeat deprecation"));

  EXPECT_EQ(store.revision(), revision);
  EXPECT_EQ(store.catalog().team({1})->name, "Arsenal Women");
  EXPECT_EQ(store.catalog().team({2})->status,
            emberdb::CanonicalEntityStatus::Deprecated);
  EXPECT_EQ(store.catalog().player({11})->status,
            emberdb::CanonicalEntityStatus::Merged);
  EXPECT_EQ(store.catalog().resolvePlayer(
                {"StatsBomb", "199", std::nullopt}),
            emberdb::CanonicalPlayerId{10});
  ASSERT_EQ(store.catalogChanges().size(), 6U);
  EXPECT_EQ(store.catalogChanges()[3].action,
            emberdb::CatalogChangeAction::Rename);
  EXPECT_EQ(store.catalogChanges()[4].action,
            emberdb::CatalogChangeAction::Merge);
  EXPECT_EQ(store.catalogChanges()[4].related_canonical_id, 10);
  EXPECT_EQ(store.catalogChanges()[5].action,
            emberdb::CatalogChangeAction::Deprecate);
  EXPECT_THROW(
      store.mergeCatalogEntity(emberdb::CatalogEntityType::Match, 100, 101,
                               provenance("Unsupported merge")),
      std::invalid_argument);
  EXPECT_THROW(
      store.mergeCatalogEntity(emberdb::CatalogEntityType::Player, 10, 10,
                               provenance("Invalid self merge")),
      std::invalid_argument);
}

TEST(MatchReviewStoreTest, AcceptsIdempotentlyAndCreatesBothMappings) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];

  store.accept(id, {100}, provenance("Metadata agrees"));
  store.accept(id, {100}, provenance("Repeated decision"));

  ASSERT_NE(store.candidate(id), nullptr);
  EXPECT_EQ(store.candidate(id)->status, emberdb::MatchCandidateStatus::Accepted);
  EXPECT_EQ(store.candidate(id)->accepted_match_id, emberdb::CanonicalMatchId{100});
  EXPECT_EQ(store.candidate(id)->decision_provenance->actor, "reviewer");
  EXPECT_EQ(store.catalog().resolveMatch({"StatsBomb", "12345"}),
            emberdb::CanonicalMatchId{100});
  EXPECT_EQ(store.catalog().resolveMatch({"Wyscout", "2499719"}),
            emberdb::CanonicalMatchId{100});
}

TEST(MatchReviewStoreTest, RejectsIdempotentlyAndPreservesReason) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];

  store.reject(
      id, provenance("Broadcast date proves these are different fixtures"));
  store.reject(
      id, provenance("Broadcast date proves these are different fixtures"));

  EXPECT_EQ(store.candidate(id)->status, emberdb::MatchCandidateStatus::Rejected);
  EXPECT_EQ(store.candidate(id)->rejection_reason,
            "Broadcast date proves these are different fixtures");
  EXPECT_TRUE(store.catalog().matchMappings().empty());
}

TEST(MatchReviewStoreTest, RejectsConflictingFinalDecisions) {
  auto accepted = reviewStore();
  const auto accepted_id = accepted.addCandidates({candidate(accepted)})[0];
  accepted.accept(accepted_id, {100}, provenance("Metadata agrees"));
  EXPECT_THROW(accepted.reject(accepted_id, provenance("Changed my mind")),
               std::invalid_argument);
  EXPECT_THROW(
      accepted.accept(accepted_id, {101}, provenance("Changed match")),
      std::invalid_argument);

  auto rejected = reviewStore();
  const auto rejected_id = rejected.addCandidates({candidate(rejected)})[0];
  rejected.reject(rejected_id, provenance("Wrong fixture"));
  EXPECT_THROW(
      rejected.accept(rejected_id, {100}, provenance("Changed my mind")),
      std::invalid_argument);
  EXPECT_THROW(rejected.reject(rejected_id, provenance("Different reason")),
               std::invalid_argument);
}

TEST(MatchReviewStoreTest, ValidatesBeforeCreatingAnyAcceptedMapping) {
  const auto original = reviewStore();
  auto catalog = original.catalog();
  catalog.mapMatch({"Wyscout", "2499719"}, {101});
  emberdb::MatchReviewStore store(std::move(catalog));
  const auto id = store.addCandidates({candidate(store)})[0];
  EXPECT_THROW(
      store.accept(id, {100}, provenance("Metadata agrees")),
      std::invalid_argument);
  EXPECT_FALSE(store.catalog().resolveMatch({"StatsBomb", "12345"}));
  EXPECT_EQ(store.candidate(id)->status,
            emberdb::MatchCandidateStatus::Unresolved);
}

TEST(MatchReviewStoreTest, RejectsBlankReasonsAndDisqualifiedComparisons) {
  auto store = reviewStore();
  const auto id = store.addCandidates({candidate(store)})[0];
  EXPECT_THROW(store.reject(id, provenance(" \t")), std::invalid_argument);
  auto comparison = candidate(store);
  comparison.is_candidate = false;
  EXPECT_THROW(static_cast<void>(store.addCandidates({comparison})),
               std::invalid_argument);
}

TEST(MatchReviewStoreTest, RejectsInvalidPersistedCandidateSnapshots) {
  auto store = reviewStore();
  auto reconciliation = candidate(store);
  reconciliation.confidence = 2.0;
  emberdb::MatchCandidateRecord record{1, reconciliation,
                                       emberdb::MatchCandidateStatus::Unresolved,
                                       std::nullopt, std::nullopt,
                                       std::nullopt};
  EXPECT_THROW(static_cast<void>(emberdb::MatchReviewStore::restore(
                   store.catalog(), {record})),
               std::invalid_argument);

  reconciliation.confidence = 1.0;
  record = {1, reconciliation, emberdb::MatchCandidateStatus::Accepted,
            emberdb::CanonicalMatchId{100}, std::nullopt, std::nullopt};
  EXPECT_THROW(static_cast<void>(emberdb::MatchReviewStore::restore(
                   store.catalog(), {record})),
               std::invalid_argument);
}

}  // namespace
