#include "emberdb/reconciliation/match_review.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace emberdb {
namespace {

bool samePair(const MatchCandidateRecord& record,
              const MatchReconciliation& reconciliation) {
  return record.reconciliation.left_match == reconciliation.left_match &&
         record.reconciliation.right_match == reconciliation.right_match;
}

bool sameEntityPair(const EntityCandidateRecord& record,
                    const EntityReconciliation& reconciliation) {
  return record.reconciliation.entity_type == reconciliation.entity_type &&
         record.reconciliation.provider_identity ==
             reconciliation.provider_identity &&
         record.reconciliation.canonical_id == reconciliation.canonical_id;
}

bool blank(std::string_view value) {
  return std::ranges::all_of(value, [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
}

void validateProvenance(const ReviewProvenance& provenance) {
  if (provenance.actor.empty() || blank(provenance.actor)) {
    throw std::invalid_argument("review actor must not be blank");
  }
  if (provenance.source.empty() || blank(provenance.source)) {
    throw std::invalid_argument("review source must not be blank");
  }
  if (provenance.reason.empty() || blank(provenance.reason)) {
    throw std::invalid_argument("review reason must not be blank");
  }
}

void requireRevisionAvailable(std::uint64_t revision) {
  if (revision == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("match review revision space is exhausted");
  }
}

bool canonicalEntityExists(const CanonicalIdentityCatalog& catalog,
                           CatalogEntityType entity_type,
                           Identifier canonical_id) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return catalog.competition({canonical_id}) != nullptr;
    case CatalogEntityType::Season:
      return catalog.season({canonical_id}) != nullptr;
    case CatalogEntityType::Team:
      return catalog.team({canonical_id}) != nullptr;
    case CatalogEntityType::Player:
      return catalog.player({canonical_id}) != nullptr;
    case CatalogEntityType::Match:
      return catalog.match({canonical_id}) != nullptr;
  }
  return false;
}

bool canonicalEntityExists(const CanonicalIdentityCatalog& catalog,
                           IdentityEntityType entity_type,
                           Identifier canonical_id) {
  switch (entity_type) {
    case IdentityEntityType::Competition:
      return catalog.competition({canonical_id}) != nullptr;
    case IdentityEntityType::Season:
      return catalog.season({canonical_id}) != nullptr;
    case IdentityEntityType::Team:
      return catalog.team({canonical_id}) != nullptr;
    case IdentityEntityType::Player:
      return catalog.player({canonical_id}) != nullptr;
  }
  return false;
}

CanonicalEntityStatus canonicalEntityStatus(
    const CanonicalIdentityCatalog& catalog, CatalogEntityType entity_type,
    Identifier canonical_id) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return catalog.competition({canonical_id})->status;
    case CatalogEntityType::Season:
      return catalog.season({canonical_id})->status;
    case CatalogEntityType::Team:
      return catalog.team({canonical_id})->status;
    case CatalogEntityType::Player:
      return catalog.player({canonical_id})->status;
    case CatalogEntityType::Match:
      return CanonicalEntityStatus::Active;
  }
  return CanonicalEntityStatus::Active;
}

std::optional<Identifier> mergedTarget(
    const CanonicalIdentityCatalog& catalog, CatalogEntityType entity_type,
    Identifier canonical_id) {
  switch (entity_type) {
    case CatalogEntityType::Competition: {
      const auto target = catalog.competition({canonical_id})->merged_into;
      return target ? std::optional<Identifier>{target->value} : std::nullopt;
    }
    case CatalogEntityType::Season: {
      const auto target = catalog.season({canonical_id})->merged_into;
      return target ? std::optional<Identifier>{target->value} : std::nullopt;
    }
    case CatalogEntityType::Team: {
      const auto target = catalog.team({canonical_id})->merged_into;
      return target ? std::optional<Identifier>{target->value} : std::nullopt;
    }
    case CatalogEntityType::Player: {
      const auto target = catalog.player({canonical_id})->merged_into;
      return target ? std::optional<Identifier>{target->value} : std::nullopt;
    }
    case CatalogEntityType::Match:
      return std::nullopt;
  }
  return std::nullopt;
}

Identifier currentCanonicalId(const CanonicalIdentityCatalog& catalog,
                              CatalogEntityType entity_type,
                              Identifier canonical_id) {
  if (canonicalEntityStatus(catalog, entity_type, canonical_id) ==
      CanonicalEntityStatus::Merged) {
    return *mergedTarget(catalog, entity_type, canonical_id);
  }
  return canonical_id;
}

CatalogEntityType catalogEntityType(IdentityEntityType entity_type) {
  switch (entity_type) {
    case IdentityEntityType::Competition:
      return CatalogEntityType::Competition;
    case IdentityEntityType::Season:
      return CatalogEntityType::Season;
    case IdentityEntityType::Team:
      return CatalogEntityType::Team;
    case IdentityEntityType::Player:
      return CatalogEntityType::Player;
  }
  return CatalogEntityType::Player;
}

bool canonicalIdsAgree(const CanonicalIdentityCatalog& catalog,
                       CatalogEntityType entity_type, Identifier expected,
                       Identifier actual) {
  return currentCanonicalId(catalog, entity_type, expected) ==
         currentCanonicalId(catalog, entity_type, actual);
}

std::string canonicalEntityName(const CanonicalIdentityCatalog& catalog,
                                CatalogEntityType entity_type,
                                Identifier canonical_id) {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return catalog.competition({canonical_id})->name;
    case CatalogEntityType::Season:
      return catalog.season({canonical_id})->name;
    case CatalogEntityType::Team:
      return catalog.team({canonical_id})->name;
    case CatalogEntityType::Player:
      return catalog.player({canonical_id})->name;
    case CatalogEntityType::Match: {
      const auto* match = catalog.match({canonical_id});
      return match->competition + " " + match->season;
    }
  }
  return {};
}

bool entityMappingMatches(const CanonicalIdentityCatalog& catalog,
                          const EntityReconciliation& reconciliation) {
  const auto& reference = reconciliation.provider_identity;
  std::optional<Identifier> mapped;
  switch (reconciliation.entity_type) {
    case IdentityEntityType::Competition:
      if (const auto id =
              catalog.resolveCompetition({reference.provider, reference.id})) {
        mapped = id->value;
      }
      break;
    case IdentityEntityType::Season:
      if (const auto id =
              catalog.resolveSeason({reference.provider, reference.id})) {
        mapped = id->value;
      }
      break;
    case IdentityEntityType::Team:
      if (const auto id = catalog.resolveTeam(
              {reference.provider, reference.id, reference.match_id})) {
        mapped = id->value;
      }
      break;
    case IdentityEntityType::Player:
      if (const auto id = catalog.resolvePlayer(
              {reference.provider, reference.id, reference.match_id})) {
        mapped = id->value;
      }
      break;
  }
  return mapped &&
         canonicalIdsAgree(catalog,
                           catalogEntityType(reconciliation.entity_type),
                           reconciliation.canonical_id, *mapped);
}

void validateEntityReconciliation(
    const CanonicalIdentityCatalog& catalog,
    const EntityReconciliation& reconciliation) {
  if (reconciliation.canonical_id <= 0 ||
      reconciliation.provider_identity.provider.empty() ||
      reconciliation.provider_identity.id.empty() ||
      reconciliation.source.empty() || blank(reconciliation.source) ||
      reconciliation.name.status != ReconciliationStatus::Agreeing ||
      !reconciliation.name.provider_value ||
      !reconciliation.name.canonical_value ||
      reconciliation.name.provider_value->empty() ||
      blank(*reconciliation.name.provider_value) ||
      reconciliation.name.canonical_value->empty() ||
      blank(*reconciliation.name.canonical_value) ||
      reconciliation.context.status == ReconciliationStatus::Conflicting ||
      !std::isfinite(reconciliation.confidence) ||
      reconciliation.confidence < 0.0 ||
      reconciliation.confidence > 1.0 ||
      !canonicalEntityExists(catalog, reconciliation.entity_type,
                             reconciliation.canonical_id)) {
    throw std::invalid_argument(
        "entity candidate contains invalid comparison data");
  }
  if ((reconciliation.entity_type == IdentityEntityType::Competition ||
       reconciliation.entity_type == IdentityEntityType::Season) &&
      reconciliation.provider_identity.match_id) {
    throw std::invalid_argument(
        "competition or season candidate has a match scope");
  }
}

bool catalogMappingMatches(const CanonicalIdentityCatalog& catalog,
                           const CatalogChangeRecord& change) {
  if (!change.provider || !change.provider_id) {
    return false;
  }
  std::optional<Identifier> mapped;
  switch (change.entity_type) {
    case CatalogEntityType::Competition:
      if (const auto id = catalog.resolveCompetition(
              {*change.provider, *change.provider_id})) {
        mapped = id->value;
      }
      break;
    case CatalogEntityType::Season:
      if (const auto id =
              catalog.resolveSeason({*change.provider, *change.provider_id})) {
        mapped = id->value;
      }
      break;
    case CatalogEntityType::Team:
      if (const auto id =
              catalog.resolveTeam({*change.provider, *change.provider_id,
                                   change.provider_match_id})) {
        mapped = id->value;
      }
      break;
    case CatalogEntityType::Player:
      if (const auto id =
              catalog.resolvePlayer({*change.provider, *change.provider_id,
                                     change.provider_match_id})) {
        mapped = id->value;
      }
      break;
    case CatalogEntityType::Match:
      return false;
  }
  return mapped &&
         canonicalIdsAgree(catalog, change.entity_type, change.canonical_id,
                           *mapped);
}

void requireCompatibleMapping(const CanonicalIdentityCatalog& catalog,
                              const ProviderMatchReference& provider_match,
                              CanonicalMatchId canonical_match) {
  const auto existing = catalog.resolveMatch(provider_match);
  if (existing && *existing != canonical_match) {
    throw std::invalid_argument(
        "provider match '" + provider_match.provider + ":" + provider_match.id +
        "' is already mapped to canonical match " +
        std::to_string(existing->value));
  }
}

}  // namespace

MatchReviewStore::MatchReviewStore(CanonicalIdentityCatalog catalog)
    : catalog_(std::move(catalog)) {}

MatchReviewStore MatchReviewStore::restore(
    CanonicalIdentityCatalog catalog,
    std::vector<MatchCandidateRecord> candidates, std::uint64_t revision,
    std::vector<CatalogChangeRecord> catalog_changes,
    std::vector<EntityCandidateRecord> entity_candidates) {
  MatchReviewStore store(std::move(catalog));
  store.revision_ = revision;
  for (const auto& record : candidates) {
    if (record.id == 0 || !record.reconciliation.is_candidate) {
      throw std::invalid_argument("persisted match candidate is invalid");
    }
    if (record.reconciliation.left_match.provider.empty() ||
        record.reconciliation.left_match.id.empty() ||
        record.reconciliation.right_match.provider.empty() ||
        record.reconciliation.right_match.id.empty() ||
        record.reconciliation.left_match == record.reconciliation.right_match ||
        !std::isfinite(record.reconciliation.confidence) ||
        record.reconciliation.confidence < 0.0 ||
        record.reconciliation.confidence > 1.0) {
      throw std::invalid_argument(
          "persisted match candidate contains invalid comparison data");
    }
    if (store.candidate(record.id) != nullptr ||
        std::ranges::any_of(store.candidates_, [&record](const auto& existing) {
          return samePair(existing, record.reconciliation);
        })) {
      throw std::invalid_argument("persisted match candidates contain duplicates");
    }
    switch (record.status) {
      case MatchCandidateStatus::Unresolved:
        if (record.accepted_match_id || record.rejection_reason ||
            record.decision_provenance) {
          throw std::invalid_argument(
              "unresolved candidate contains finalized decision data");
        }
        break;
      case MatchCandidateStatus::Accepted:
        if (!record.accepted_match_id || record.rejection_reason ||
            store.catalog_.match(*record.accepted_match_id) == nullptr ||
            store.catalog_.resolveMatch(record.reconciliation.left_match) !=
                record.accepted_match_id ||
            store.catalog_.resolveMatch(record.reconciliation.right_match) !=
                record.accepted_match_id) {
          throw std::invalid_argument(
              "accepted candidate does not match its durable catalog mappings");
        }
        break;
      case MatchCandidateStatus::Rejected:
        if (record.accepted_match_id || !record.rejection_reason ||
            record.rejection_reason->empty() || blank(*record.rejection_reason)) {
          throw std::invalid_argument("rejected candidate has invalid decision data");
        }
        break;
    }
    if (record.decision_provenance) {
      validateProvenance(*record.decision_provenance);
    }
    if (record.id >= std::numeric_limits<std::uint64_t>::max() - 1U) {
      throw std::invalid_argument("persisted match candidate ID is too large");
    }
    store.next_candidate_id_ = std::max(store.next_candidate_id_, record.id + 1U);
    store.candidates_.push_back(record);
  }
  std::ranges::sort(store.candidates_, {}, &MatchCandidateRecord::id);
  std::uint64_t previous_revision{};
  for (const auto& change : catalog_changes) {
    if (change.revision == 0 || change.revision > store.revision_ ||
        change.revision <= previous_revision || change.canonical_id <= 0 ||
        change.canonical_name.empty() || blank(change.canonical_name)) {
      throw std::invalid_argument(
          "persisted catalog change contains invalid revision or identity data");
    }
    validateProvenance(change.provenance);
    if (!canonicalEntityExists(store.catalog_, change.entity_type,
                               change.canonical_id)) {
      throw std::invalid_argument(
          "persisted catalog change references an unknown canonical entity");
    }
    switch (change.action) {
      case CatalogChangeAction::Add:
      case CatalogChangeAction::Rename:
        if (change.provider || change.provider_id ||
            change.provider_match_id || change.related_canonical_id) {
          throw std::invalid_argument(
              "persisted catalog change contains unexpected related data");
        }
        break;
      case CatalogChangeAction::Deprecate:
        if (change.provider || change.provider_id ||
            change.provider_match_id || change.related_canonical_id ||
            canonicalEntityStatus(store.catalog_, change.entity_type,
                                  change.canonical_id) !=
                CanonicalEntityStatus::Deprecated) {
          throw std::invalid_argument(
              "persisted catalog deprecation does not match the durable catalog");
        }
        break;
      case CatalogChangeAction::Map:
        if (change.related_canonical_id || !change.provider ||
            !change.provider_id || change.provider->empty() ||
            change.provider_id->empty() ||
            !catalogMappingMatches(store.catalog_, change)) {
          throw std::invalid_argument(
              "persisted catalog mapping does not match the durable catalog");
        }
        if ((change.entity_type == CatalogEntityType::Competition ||
             change.entity_type == CatalogEntityType::Season) &&
            change.provider_match_id) {
          throw std::invalid_argument(
              "persisted competition or season mapping has a match scope");
        }
        break;
      case CatalogChangeAction::Merge:
        if (change.provider || change.provider_id ||
            change.provider_match_id || !change.related_canonical_id ||
            *change.related_canonical_id <= 0 ||
            change.entity_type == CatalogEntityType::Match ||
            !canonicalEntityExists(store.catalog_, change.entity_type,
                                   *change.related_canonical_id) ||
            canonicalEntityStatus(store.catalog_, change.entity_type,
                                  change.canonical_id) !=
                CanonicalEntityStatus::Merged ||
            !canonicalIdsAgree(store.catalog_, change.entity_type,
                               change.canonical_id,
                               *change.related_canonical_id)) {
          throw std::invalid_argument(
              "persisted catalog merge does not match the durable catalog");
        }
        break;
    }
    previous_revision = change.revision;
    store.catalog_changes_.push_back(change);
  }
  for (const auto& record : entity_candidates) {
    const auto& reconciliation = record.reconciliation;
    if (record.id == 0) {
      throw std::invalid_argument("persisted entity candidate has an invalid ID");
    }
    validateEntityReconciliation(store.catalog_, reconciliation);
    if (store.entityCandidate(record.id) != nullptr ||
        std::ranges::any_of(
            store.entity_candidates_, [&reconciliation](const auto& existing) {
              return sameEntityPair(existing, reconciliation);
            })) {
      throw std::invalid_argument(
          "persisted entity candidates contain duplicates");
    }
    switch (record.status) {
      case MatchCandidateStatus::Unresolved:
        if (record.rejection_reason || record.decision_provenance) {
          throw std::invalid_argument(
              "unresolved entity candidate contains finalized decision data");
        }
        break;
      case MatchCandidateStatus::Accepted:
        if (record.rejection_reason || !record.decision_provenance ||
            !entityMappingMatches(store.catalog_, reconciliation)) {
          throw std::invalid_argument(
              "accepted entity candidate does not match its durable catalog mapping");
        }
        break;
      case MatchCandidateStatus::Rejected:
        if (!record.rejection_reason || !record.decision_provenance ||
            record.rejection_reason->empty() ||
            blank(*record.rejection_reason)) {
          throw std::invalid_argument(
              "rejected entity candidate has invalid decision data");
        }
        break;
    }
    if (record.decision_provenance) {
      validateProvenance(*record.decision_provenance);
    }
    if (record.id >= std::numeric_limits<std::uint64_t>::max() - 1U) {
      throw std::invalid_argument(
          "persisted entity candidate ID is too large");
    }
    store.next_entity_candidate_id_ =
        std::max(store.next_entity_candidate_id_, record.id + 1U);
    store.entity_candidates_.push_back(record);
  }
  std::ranges::sort(store.entity_candidates_, {},
                    &EntityCandidateRecord::id);
  return store;
}

const CanonicalIdentityCatalog& MatchReviewStore::catalog() const noexcept {
  return catalog_;
}

std::uint64_t MatchReviewStore::revision() const noexcept { return revision_; }

const std::vector<CatalogChangeRecord>& MatchReviewStore::catalogChanges()
    const noexcept {
  return catalog_changes_;
}

void MatchReviewStore::addCompetition(CanonicalCompetition competition,
                                      ReviewProvenance provenance) {
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto id = competition.id.value;
  const auto name = competition.name;
  catalog_.addCompetition(std::move(competition));
  recordCatalogChange(CatalogChangeAction::Add,
                      CatalogEntityType::Competition, id, name, std::nullopt,
                      std::nullopt, std::nullopt, std::move(provenance));
}

void MatchReviewStore::addSeason(CanonicalSeason season,
                                 ReviewProvenance provenance) {
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto id = season.id.value;
  const auto name = season.name;
  catalog_.addSeason(std::move(season));
  recordCatalogChange(CatalogChangeAction::Add, CatalogEntityType::Season, id,
                      name, std::nullopt, std::nullopt, std::nullopt,
                      std::move(provenance));
}

void MatchReviewStore::addTeam(CanonicalTeam team,
                               ReviewProvenance provenance) {
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto id = team.id.value;
  const auto name = team.name;
  catalog_.addTeam(std::move(team));
  recordCatalogChange(CatalogChangeAction::Add, CatalogEntityType::Team, id,
                      name, std::nullopt, std::nullopt, std::nullopt,
                      std::move(provenance));
}

void MatchReviewStore::addPlayer(CanonicalPlayer player,
                                 ReviewProvenance provenance) {
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto id = player.id.value;
  const auto name = player.name;
  catalog_.addPlayer(std::move(player));
  recordCatalogChange(CatalogChangeAction::Add, CatalogEntityType::Player, id,
                      name, std::nullopt, std::nullopt, std::nullopt,
                      std::move(provenance));
}

void MatchReviewStore::addMatch(CanonicalMatch match,
                                ReviewProvenance provenance) {
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto id = match.id.value;
  const auto name = match.competition + " " + match.season;
  catalog_.addMatch(std::move(match));
  recordCatalogChange(CatalogChangeAction::Add, CatalogEntityType::Match, id,
                      name, std::nullopt, std::nullopt, std::nullopt,
                      std::move(provenance));
}

void MatchReviewStore::mapCompetition(
    ProviderCompetitionReference provider_competition,
    CanonicalCompetitionId canonical_competition,
    ReviewProvenance provenance) {
  if (catalog_.resolveCompetition(provider_competition) ==
      canonical_competition) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto provider = provider_competition.provider;
  const auto provider_id = provider_competition.id;
  catalog_.mapCompetition(std::move(provider_competition),
                          canonical_competition);
  recordCatalogChange(
      CatalogChangeAction::Map, CatalogEntityType::Competition,
      canonical_competition.value,
      catalog_.competition(canonical_competition)->name, std::move(provider),
      std::move(provider_id), std::nullopt, std::move(provenance));
}

void MatchReviewStore::mapSeason(ProviderSeasonReference provider_season,
                                 CanonicalSeasonId canonical_season,
                                 ReviewProvenance provenance) {
  if (catalog_.resolveSeason(provider_season) == canonical_season) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto provider = provider_season.provider;
  const auto provider_id = provider_season.id;
  catalog_.mapSeason(std::move(provider_season), canonical_season);
  recordCatalogChange(
      CatalogChangeAction::Map, CatalogEntityType::Season,
      canonical_season.value, catalog_.season(canonical_season)->name,
      std::move(provider), std::move(provider_id), std::nullopt,
      std::move(provenance));
}

void MatchReviewStore::mapTeam(ProviderTeamReference provider_team,
                               CanonicalTeamId canonical_team,
                               ReviewProvenance provenance) {
  if (catalog_.resolveTeam(provider_team) == canonical_team) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto provider = provider_team.provider;
  const auto provider_id = provider_team.id;
  const auto provider_match_id = provider_team.match_id;
  catalog_.mapTeam(std::move(provider_team), canonical_team);
  recordCatalogChange(
      CatalogChangeAction::Map, CatalogEntityType::Team,
      canonical_team.value, catalog_.team(canonical_team)->name,
      std::move(provider), std::move(provider_id),
      std::move(provider_match_id), std::move(provenance));
}

void MatchReviewStore::mapPlayer(ProviderPlayerReference provider_player,
                                 CanonicalPlayerId canonical_player,
                                 ReviewProvenance provenance) {
  if (catalog_.resolvePlayer(provider_player) == canonical_player) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto provider = provider_player.provider;
  const auto provider_id = provider_player.id;
  const auto provider_match_id = provider_player.match_id;
  catalog_.mapPlayer(std::move(provider_player), canonical_player);
  recordCatalogChange(
      CatalogChangeAction::Map, CatalogEntityType::Player,
      canonical_player.value, catalog_.player(canonical_player)->name,
      std::move(provider), std::move(provider_id),
      std::move(provider_match_id), std::move(provenance));
}

void MatchReviewStore::renameCatalogEntity(
    CatalogEntityType entity_type, Identifier canonical_id, std::string name,
    ReviewProvenance provenance) {
  if (entity_type == CatalogEntityType::Match) {
    throw std::invalid_argument(
        "canonical match labels cannot be renamed independently");
  }
  if (!canonicalEntityExists(catalog_, entity_type, canonical_id)) {
    throw std::invalid_argument("cannot rename an unknown canonical " +
                                std::string(catalogEntityTypeName(entity_type)) +
                                " ID");
  }
  if (canonicalEntityName(catalog_, entity_type, canonical_id) == name) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  switch (entity_type) {
    case CatalogEntityType::Competition:
      catalog_.renameCompetition({canonical_id}, std::move(name));
      break;
    case CatalogEntityType::Season:
      catalog_.renameSeason({canonical_id}, std::move(name));
      break;
    case CatalogEntityType::Team:
      catalog_.renameTeam({canonical_id}, std::move(name));
      break;
    case CatalogEntityType::Player:
      catalog_.renamePlayer({canonical_id}, std::move(name));
      break;
    case CatalogEntityType::Match:
      break;
  }
  recordCatalogChange(
      CatalogChangeAction::Rename, entity_type, canonical_id,
      canonicalEntityName(catalog_, entity_type, canonical_id), std::nullopt,
      std::nullopt, std::nullopt, std::move(provenance));
}

void MatchReviewStore::deprecateCatalogEntity(
    CatalogEntityType entity_type, Identifier canonical_id,
    ReviewProvenance provenance) {
  if (entity_type == CatalogEntityType::Match) {
    throw std::invalid_argument(
        "canonical match deprecation is not supported");
  }
  if (!canonicalEntityExists(catalog_, entity_type, canonical_id)) {
    throw std::invalid_argument("cannot deprecate an unknown canonical " +
                                std::string(catalogEntityTypeName(entity_type)) +
                                " ID");
  }
  if (canonicalEntityStatus(catalog_, entity_type, canonical_id) ==
      CanonicalEntityStatus::Deprecated) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto name =
      canonicalEntityName(catalog_, entity_type, canonical_id);
  switch (entity_type) {
    case CatalogEntityType::Competition:
      catalog_.deprecateCompetition({canonical_id});
      break;
    case CatalogEntityType::Season:
      catalog_.deprecateSeason({canonical_id});
      break;
    case CatalogEntityType::Team:
      catalog_.deprecateTeam({canonical_id});
      break;
    case CatalogEntityType::Player:
      catalog_.deprecatePlayer({canonical_id});
      break;
    case CatalogEntityType::Match:
      break;
  }
  recordCatalogChange(CatalogChangeAction::Deprecate, entity_type,
                      canonical_id, name, std::nullopt, std::nullopt,
                      std::nullopt, std::move(provenance));
}

void MatchReviewStore::mergeCatalogEntity(
    CatalogEntityType entity_type, Identifier source_canonical_id,
    Identifier target_canonical_id, ReviewProvenance provenance) {
  if (entity_type == CatalogEntityType::Match) {
    throw std::invalid_argument("canonical match merging is not supported");
  }
  if (source_canonical_id == target_canonical_id) {
    throw std::invalid_argument(
        "canonical entity cannot be merged into itself");
  }
  if (!canonicalEntityExists(catalog_, entity_type, source_canonical_id) ||
      !canonicalEntityExists(catalog_, entity_type, target_canonical_id)) {
    throw std::invalid_argument(
        "catalog merge requires existing source and target canonical IDs");
  }
  if (canonicalEntityStatus(catalog_, entity_type, source_canonical_id) ==
          CanonicalEntityStatus::Merged &&
      canonicalIdsAgree(catalog_, entity_type, source_canonical_id,
                        target_canonical_id)) {
    return;
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto source_name =
      canonicalEntityName(catalog_, entity_type, source_canonical_id);
  switch (entity_type) {
    case CatalogEntityType::Competition:
      catalog_.mergeCompetition({source_canonical_id},
                                {target_canonical_id});
      break;
    case CatalogEntityType::Season:
      catalog_.mergeSeason({source_canonical_id}, {target_canonical_id});
      break;
    case CatalogEntityType::Team:
      catalog_.mergeTeam({source_canonical_id}, {target_canonical_id});
      break;
    case CatalogEntityType::Player:
      catalog_.mergePlayer({source_canonical_id}, {target_canonical_id});
      break;
    case CatalogEntityType::Match:
      break;
  }
  recordCatalogChange(
      CatalogChangeAction::Merge, entity_type, source_canonical_id,
      source_name, std::nullopt, std::nullopt, std::nullopt,
      std::move(provenance), target_canonical_id);
}

std::vector<std::uint64_t> MatchReviewStore::addCandidates(
    const std::vector<MatchReconciliation>& candidates) {
  std::vector<std::uint64_t> ids;
  ids.reserve(candidates.size());
  bool added_candidate = false;
  for (const auto& reconciliation : candidates) {
    if (!reconciliation.is_candidate) {
      throw std::invalid_argument("cannot review a disqualified match comparison");
    }
    const auto existing = std::ranges::find_if(
        candidates_, [&reconciliation](const MatchCandidateRecord& record) {
          return samePair(record, reconciliation);
        });
    if (existing != candidates_.end()) {
      ids.push_back(existing->id);
      continue;
    }
    if (next_candidate_id_ == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("match candidate ID space is exhausted");
    }
    if (!added_candidate) {
      requireRevisionAvailable(revision_);
    }
    const auto id = next_candidate_id_++;
    candidates_.push_back(
        {id, reconciliation, MatchCandidateStatus::Unresolved, std::nullopt,
         std::nullopt, std::nullopt});
    added_candidate = true;
    ids.push_back(id);
  }
  if (added_candidate) {
    advanceRevision();
  }
  return ids;
}

const MatchCandidateRecord* MatchReviewStore::candidate(std::uint64_t id) const {
  const auto position = std::ranges::find(candidates_, id,
                                          &MatchCandidateRecord::id);
  return position == candidates_.end() ? nullptr : &*position;
}

std::vector<const MatchCandidateRecord*> MatchReviewStore::candidates(
    std::optional<MatchCandidateStatus> status) const {
  std::vector<const MatchCandidateRecord*> result;
  for (const auto& candidate : candidates_) {
    if (!status || candidate.status == *status) {
      result.push_back(&candidate);
    }
  }
  return result;
}

std::vector<std::uint64_t> MatchReviewStore::addEntityCandidates(
    const std::vector<EntityReconciliation>& candidates) {
  std::vector<std::uint64_t> ids;
  ids.reserve(candidates.size());
  bool added_candidate = false;
  for (const auto& reconciliation : candidates) {
    validateEntityReconciliation(catalog_, reconciliation);
    const auto existing = std::ranges::find_if(
        entity_candidates_,
        [&reconciliation](const EntityCandidateRecord& record) {
          return sameEntityPair(record, reconciliation);
        });
    if (existing != entity_candidates_.end()) {
      ids.push_back(existing->id);
      continue;
    }
    if (next_entity_candidate_id_ ==
        std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error(
          "entity candidate ID space is exhausted");
    }
    if (!added_candidate) {
      requireRevisionAvailable(revision_);
    }
    const auto id = next_entity_candidate_id_++;
    entity_candidates_.push_back(
        {id, reconciliation, MatchCandidateStatus::Unresolved, std::nullopt,
         std::nullopt});
    added_candidate = true;
    ids.push_back(id);
  }
  if (added_candidate) {
    advanceRevision();
  }
  return ids;
}

const EntityCandidateRecord* MatchReviewStore::entityCandidate(
    std::uint64_t id) const {
  const auto position = std::ranges::find(
      entity_candidates_, id, &EntityCandidateRecord::id);
  return position == entity_candidates_.end() ? nullptr : &*position;
}

std::vector<const EntityCandidateRecord*> MatchReviewStore::entityCandidates(
    std::optional<MatchCandidateStatus> status,
    std::optional<IdentityEntityType> entity_type) const {
  std::vector<const EntityCandidateRecord*> result;
  for (const auto& candidate : entity_candidates_) {
    if ((!status || candidate.status == *status) &&
        (!entity_type ||
         candidate.reconciliation.entity_type == *entity_type)) {
      result.push_back(&candidate);
    }
  }
  return result;
}

void MatchReviewStore::accept(std::uint64_t candidate_id,
                              CanonicalMatchId canonical_match_id,
                              ReviewProvenance provenance) {
  auto& record = requireCandidate(candidate_id);
  if (record.status == MatchCandidateStatus::Accepted) {
    if (record.accepted_match_id == canonical_match_id) {
      return;
    }
    throw std::invalid_argument("candidate " + std::to_string(candidate_id) +
                                " is already accepted for canonical match " +
                                std::to_string(record.accepted_match_id->value));
  }
  if (record.status == MatchCandidateStatus::Rejected) {
    throw std::invalid_argument("candidate " + std::to_string(candidate_id) +
                                " is already rejected");
  }
  if (catalog_.match(canonical_match_id) == nullptr) {
    throw std::invalid_argument("cannot accept candidate for unknown canonical match " +
                                std::to_string(canonical_match_id.value));
  }
  requireCompatibleMapping(catalog_, record.reconciliation.left_match,
                           canonical_match_id);
  requireCompatibleMapping(catalog_, record.reconciliation.right_match,
                           canonical_match_id);
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  catalog_.mapMatch(record.reconciliation.left_match, canonical_match_id);
  catalog_.mapMatch(record.reconciliation.right_match, canonical_match_id);
  record.status = MatchCandidateStatus::Accepted;
  record.accepted_match_id = canonical_match_id;
  record.decision_provenance = std::move(provenance);
  advanceRevision();
}

void MatchReviewStore::reject(std::uint64_t candidate_id,
                              ReviewProvenance provenance) {
  validateProvenance(provenance);
  const auto& reason = provenance.reason;
  auto& record = requireCandidate(candidate_id);
  if (record.status == MatchCandidateStatus::Rejected) {
    if (record.rejection_reason == reason) {
      return;
    }
    throw std::invalid_argument("candidate " + std::to_string(candidate_id) +
                                " is already rejected with a different reason");
  }
  if (record.status == MatchCandidateStatus::Accepted) {
    throw std::invalid_argument("candidate " + std::to_string(candidate_id) +
                                " is already accepted");
  }
  requireRevisionAvailable(revision_);
  record.status = MatchCandidateStatus::Rejected;
  record.rejection_reason = reason;
  record.decision_provenance = std::move(provenance);
  advanceRevision();
}

void MatchReviewStore::acceptEntityCandidate(
    std::uint64_t candidate_id, ReviewProvenance provenance) {
  auto& record = requireEntityCandidate(candidate_id);
  if (record.status == MatchCandidateStatus::Accepted) {
    return;
  }
  if (record.status == MatchCandidateStatus::Rejected) {
    throw std::invalid_argument(
        "entity candidate " + std::to_string(candidate_id) +
        " is already rejected");
  }
  validateProvenance(provenance);
  requireRevisionAvailable(revision_);
  const auto revision_before_mapping = revision_;
  const auto& reconciliation = record.reconciliation;
  const auto& reference = reconciliation.provider_identity;
  switch (reconciliation.entity_type) {
    case IdentityEntityType::Competition:
      mapCompetition({reference.provider, reference.id},
                     {reconciliation.canonical_id}, provenance);
      break;
    case IdentityEntityType::Season:
      mapSeason({reference.provider, reference.id},
                {reconciliation.canonical_id}, provenance);
      break;
    case IdentityEntityType::Team:
      mapTeam({reference.provider, reference.id, reference.match_id},
              {reconciliation.canonical_id}, provenance);
      break;
    case IdentityEntityType::Player:
      mapPlayer({reference.provider, reference.id, reference.match_id},
                {reconciliation.canonical_id}, provenance);
      break;
  }
  record.status = MatchCandidateStatus::Accepted;
  record.decision_provenance = std::move(provenance);
  if (revision_ == revision_before_mapping) {
    advanceRevision();
  }
}

void MatchReviewStore::rejectEntityCandidate(
    std::uint64_t candidate_id, ReviewProvenance provenance) {
  validateProvenance(provenance);
  const auto& reason = provenance.reason;
  auto& record = requireEntityCandidate(candidate_id);
  if (record.status == MatchCandidateStatus::Rejected) {
    if (record.rejection_reason == reason) {
      return;
    }
    throw std::invalid_argument(
        "entity candidate " + std::to_string(candidate_id) +
        " is already rejected with a different reason");
  }
  if (record.status == MatchCandidateStatus::Accepted) {
    throw std::invalid_argument(
        "entity candidate " + std::to_string(candidate_id) +
        " is already accepted");
  }
  requireRevisionAvailable(revision_);
  record.status = MatchCandidateStatus::Rejected;
  record.rejection_reason = reason;
  record.decision_provenance = std::move(provenance);
  advanceRevision();
}

void MatchReviewStore::recordCatalogChange(
    CatalogChangeAction action, CatalogEntityType entity_type,
    Identifier canonical_id, std::string canonical_name,
    std::optional<std::string> provider, std::optional<std::string> provider_id,
    std::optional<std::string> provider_match_id,
    ReviewProvenance provenance,
    std::optional<Identifier> related_canonical_id) {
  advanceRevision();
  catalog_changes_.push_back(
      {revision_, action, entity_type, canonical_id, std::move(canonical_name),
       std::move(provider), std::move(provider_id),
       std::move(provider_match_id), std::move(provenance),
       related_canonical_id});
}

void MatchReviewStore::advanceRevision() {
  requireRevisionAvailable(revision_);
  ++revision_;
}

MatchCandidateRecord& MatchReviewStore::requireCandidate(std::uint64_t id) {
  const auto position = std::ranges::find(candidates_, id,
                                          &MatchCandidateRecord::id);
  if (position == candidates_.end()) {
    throw std::invalid_argument("unknown match candidate " + std::to_string(id));
  }
  return *position;
}

EntityCandidateRecord& MatchReviewStore::requireEntityCandidate(
    std::uint64_t id) {
  const auto position = std::ranges::find(
      entity_candidates_, id, &EntityCandidateRecord::id);
  if (position == entity_candidates_.end()) {
    throw std::invalid_argument(
        "unknown entity candidate " + std::to_string(id));
  }
  return *position;
}

std::string_view matchCandidateStatusName(MatchCandidateStatus status) noexcept {
  switch (status) {
    case MatchCandidateStatus::Unresolved:
      return "unresolved";
    case MatchCandidateStatus::Accepted:
      return "accepted";
    case MatchCandidateStatus::Rejected:
      return "rejected";
  }
  return "unknown";
}

std::string_view catalogChangeActionName(CatalogChangeAction action) noexcept {
  switch (action) {
    case CatalogChangeAction::Add:
      return "add";
    case CatalogChangeAction::Map:
      return "map";
    case CatalogChangeAction::Rename:
      return "rename";
    case CatalogChangeAction::Deprecate:
      return "deprecate";
    case CatalogChangeAction::Merge:
      return "merge";
  }
  return "unknown";
}

std::string_view catalogEntityTypeName(
    CatalogEntityType entity_type) noexcept {
  switch (entity_type) {
    case CatalogEntityType::Competition:
      return "competition";
    case CatalogEntityType::Season:
      return "season";
    case CatalogEntityType::Team:
      return "team";
    case CatalogEntityType::Player:
      return "player";
    case CatalogEntityType::Match:
      return "match";
  }
  return "unknown";
}

}  // namespace emberdb
