#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/reconciliation/entity_reconciliation.h"
#include "emberdb/reconciliation/match_reconciliation.h"

namespace emberdb {

enum class MatchCandidateStatus { Unresolved, Accepted, Rejected };

struct ReviewProvenance {
  std::string actor;
  std::string source;
  std::string reason;
  std::chrono::sys_seconds recorded_at;
};

enum class CatalogChangeAction { Add, Map, Rename, Deprecate, Merge };
enum class CatalogEntityType { Competition, Season, Team, Player, Match };

struct CatalogChangeRecord {
  std::uint64_t revision{};
  CatalogChangeAction action{CatalogChangeAction::Add};
  CatalogEntityType entity_type{CatalogEntityType::Team};
  Identifier canonical_id{};
  std::string canonical_name;
  std::optional<std::string> provider;
  std::optional<std::string> provider_id;
  std::optional<std::string> provider_match_id;
  ReviewProvenance provenance;
  std::optional<Identifier> related_canonical_id;
};

struct MatchCandidateRecord {
  std::uint64_t id{};
  MatchReconciliation reconciliation;
  MatchCandidateStatus status{MatchCandidateStatus::Unresolved};
  std::optional<CanonicalMatchId> accepted_match_id;
  std::optional<std::string> rejection_reason;
  std::optional<ReviewProvenance> decision_provenance;
};

struct EntityCandidateRecord {
  std::uint64_t id{};
  EntityReconciliation reconciliation;
  MatchCandidateStatus status{MatchCandidateStatus::Unresolved};
  std::optional<std::string> rejection_reason;
  std::optional<ReviewProvenance> decision_provenance;
};

class MatchReviewStore {
 public:
  MatchReviewStore() = default;
  explicit MatchReviewStore(CanonicalIdentityCatalog catalog);
  [[nodiscard]] static MatchReviewStore restore(
      CanonicalIdentityCatalog catalog,
      std::vector<MatchCandidateRecord> candidates,
      std::uint64_t revision = 0,
      std::vector<CatalogChangeRecord> catalog_changes = {},
      std::vector<EntityCandidateRecord> entity_candidates = {});

  [[nodiscard]] const CanonicalIdentityCatalog& catalog() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;
  [[nodiscard]] const std::vector<CatalogChangeRecord>& catalogChanges()
      const noexcept;

  void addCompetition(CanonicalCompetition competition,
                      ReviewProvenance provenance);
  void addSeason(CanonicalSeason season, ReviewProvenance provenance);
  void addTeam(CanonicalTeam team, ReviewProvenance provenance);
  void addPlayer(CanonicalPlayer player, ReviewProvenance provenance);
  void addMatch(CanonicalMatch match, ReviewProvenance provenance);
  void mapCompetition(ProviderCompetitionReference provider_competition,
                      CanonicalCompetitionId canonical_competition,
                      ReviewProvenance provenance);
  void mapSeason(ProviderSeasonReference provider_season,
                 CanonicalSeasonId canonical_season,
                 ReviewProvenance provenance);
  void mapTeam(ProviderTeamReference provider_team,
               CanonicalTeamId canonical_team, ReviewProvenance provenance);
  void mapPlayer(ProviderPlayerReference provider_player,
                 CanonicalPlayerId canonical_player,
                 ReviewProvenance provenance);
  void renameCatalogEntity(CatalogEntityType entity_type,
                           Identifier canonical_id, std::string name,
                           ReviewProvenance provenance);
  void deprecateCatalogEntity(CatalogEntityType entity_type,
                              Identifier canonical_id,
                              ReviewProvenance provenance);
  void mergeCatalogEntity(CatalogEntityType entity_type,
                          Identifier source_canonical_id,
                          Identifier target_canonical_id,
                          ReviewProvenance provenance);

  [[nodiscard]] std::vector<std::uint64_t> addCandidates(
      const std::vector<MatchReconciliation>& candidates);
  [[nodiscard]] const MatchCandidateRecord* candidate(std::uint64_t id) const;
  [[nodiscard]] std::vector<const MatchCandidateRecord*> candidates(
      std::optional<MatchCandidateStatus> status = std::nullopt) const;
  [[nodiscard]] std::vector<std::uint64_t> addEntityCandidates(
      const std::vector<EntityReconciliation>& candidates);
  [[nodiscard]] const EntityCandidateRecord* entityCandidate(
      std::uint64_t id) const;
  [[nodiscard]] std::vector<const EntityCandidateRecord*> entityCandidates(
      std::optional<MatchCandidateStatus> status = std::nullopt,
      std::optional<IdentityEntityType> entity_type = std::nullopt) const;

  void accept(std::uint64_t candidate_id, CanonicalMatchId canonical_match_id,
              ReviewProvenance provenance);
  void reject(std::uint64_t candidate_id, ReviewProvenance provenance);
  void acceptEntityCandidate(std::uint64_t candidate_id,
                             ReviewProvenance provenance);
  void rejectEntityCandidate(std::uint64_t candidate_id,
                             ReviewProvenance provenance);

 private:
  [[nodiscard]] MatchCandidateRecord& requireCandidate(std::uint64_t id);
  [[nodiscard]] EntityCandidateRecord& requireEntityCandidate(
      std::uint64_t id);
  void recordCatalogChange(CatalogChangeAction action,
                           CatalogEntityType entity_type,
                           Identifier canonical_id,
                           std::string canonical_name,
                           std::optional<std::string> provider,
                           std::optional<std::string> provider_id,
                           std::optional<std::string> provider_match_id,
                           ReviewProvenance provenance,
                           std::optional<Identifier> related_canonical_id =
                               std::nullopt);
  void advanceRevision();

  CanonicalIdentityCatalog catalog_;
  std::vector<MatchCandidateRecord> candidates_;
  std::vector<EntityCandidateRecord> entity_candidates_;
  std::vector<CatalogChangeRecord> catalog_changes_;
  std::uint64_t revision_{};
  std::uint64_t next_candidate_id_{1};
  std::uint64_t next_entity_candidate_id_{1};
};

[[nodiscard]] std::string_view matchCandidateStatusName(
    MatchCandidateStatus status) noexcept;
[[nodiscard]] std::string_view catalogChangeActionName(
    CatalogChangeAction action) noexcept;
[[nodiscard]] std::string_view catalogEntityTypeName(
    CatalogEntityType entity_type) noexcept;

}  // namespace emberdb
