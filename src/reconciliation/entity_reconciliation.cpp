#include "emberdb/reconciliation/entity_reconciliation.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace emberdb {
namespace {

bool blank(std::string_view value) {
  return std::ranges::all_of(value, [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
}

std::string normalizedName(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  bool pending_space = false;
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isspace(byte) != 0) {
      pending_space = !normalized.empty();
      continue;
    }
    if (pending_space) {
      normalized.push_back(' ');
      pending_space = false;
    }
    normalized.push_back(static_cast<char>(std::tolower(byte)));
  }
  return normalized;
}

std::string providerText(const ProviderCompetitionReference& reference) {
  return reference.provider + ":" + reference.id;
}

template <typename Reference>
void requireReference(const Reference& reference) {
  if (reference.provider.empty() || reference.id.empty()) {
    throw std::invalid_argument(
        "provider entity metadata requires non-empty provider and ID");
  }
}

template <typename Reference, typename Metadata, typename Name>
std::vector<const Metadata*> uniqueMetadata(
    const std::vector<Metadata>& metadata, Reference Metadata::*reference,
    Name Metadata::*name) {
  std::map<Reference, const Metadata*> unique;
  for (const auto& record : metadata) {
    requireReference(record.*reference);
    const auto& candidate_name = record.*name;
    if constexpr (std::is_same_v<Name, std::string>) {
      if (candidate_name.empty() || blank(candidate_name)) {
        throw std::invalid_argument(
            "provider entity metadata name must not be blank");
      }
    }
    const auto [position, inserted] =
        unique.emplace(record.*reference, &record);
    if (!inserted && position->second->*name != candidate_name) {
      throw std::invalid_argument(
          "provider entity metadata contains conflicting duplicate records");
    }
  }
  std::vector<const Metadata*> result;
  result.reserve(unique.size());
  for (const auto& [unused, record] : unique) {
    static_cast<void>(unused);
    result.push_back(record);
  }
  return result;
}

template <typename CanonicalId>
EntityReconciliation candidate(
    IdentityEntityType entity_type, ProviderIdentityReference reference,
    CanonicalId canonical_id, std::string source,
    const std::string& provider_name, const std::string& canonical_name,
    EntityFieldEvidence context, double confidence) {
  return {entity_type,
          std::move(reference),
          canonical_id.value,
          std::move(source),
          {ReconciliationStatus::Agreeing, provider_name, canonical_name},
          std::move(context),
          confidence};
}

EntityFieldEvidence missingContext() {
  return {ReconciliationStatus::Missing, std::nullopt, std::nullopt};
}

void sortCandidates(std::vector<EntityReconciliation>& candidates) {
  std::ranges::sort(candidates, [](const auto& left, const auto& right) {
    return std::tie(left.provider_identity.provider, left.provider_identity.id,
                    left.provider_identity.match_id, left.canonical_id) <
           std::tie(right.provider_identity.provider, right.provider_identity.id,
                    right.provider_identity.match_id, right.canonical_id);
  });
}

}  // namespace

std::vector<EntityReconciliation> findEntityCandidates(
    const ProviderMetadata& metadata, IdentityEntityType entity_type,
    const CanonicalIdentityCatalog& catalog, std::string source) {
  if (source.empty() || blank(source)) {
    throw std::invalid_argument(
        "entity candidate source must not be blank");
  }
  std::vector<EntityReconciliation> candidates;
  switch (entity_type) {
    case IdentityEntityType::Competition:
      for (const auto* provider : uniqueMetadata(
               metadata.competitions,
               &ProviderCompetitionMetadata::reference,
               &ProviderCompetitionMetadata::name)) {
        if (catalog.resolveCompetition(provider->reference)) {
          continue;
        }
        for (const auto& [id, canonical] : catalog.competitions()) {
          if (canonical.status != CanonicalEntityStatus::Active) {
            continue;
          }
          if (normalizedName(provider->name) ==
              normalizedName(canonical.name)) {
            candidates.push_back(candidate(
                entity_type,
                {provider->reference.provider, provider->reference.id,
                 std::nullopt},
                id, source, provider->name, canonical.name, missingContext(),
                1.0));
          }
        }
      }
      break;
    case IdentityEntityType::Season:
      for (const auto* provider :
           uniqueMetadata(metadata.seasons,
                          &ProviderSeasonMetadata::reference,
                          &ProviderSeasonMetadata::name)) {
        requireReference(provider->competition);
        if (catalog.resolveSeason(provider->reference) || !provider->name ||
            provider->name->empty() || blank(*provider->name)) {
          continue;
        }
        const auto resolved_competition =
            catalog.resolveCompetition(provider->competition);
        for (const auto& [id, canonical] : catalog.seasons()) {
          if (canonical.status != CanonicalEntityStatus::Active) {
            continue;
          }
          if (normalizedName(*provider->name) !=
              normalizedName(canonical.name)) {
            continue;
          }
          EntityFieldEvidence context{
              ReconciliationStatus::Missing,
              providerText(provider->competition),
              std::to_string(canonical.competition_id.value)};
          double confidence = 0.9;
          if (resolved_competition) {
            if (*resolved_competition != canonical.competition_id) {
              continue;
            }
            context.status = ReconciliationStatus::Agreeing;
            confidence = 1.0;
          }
          candidates.push_back(candidate(
              entity_type,
              {provider->reference.provider, provider->reference.id,
               std::nullopt},
              id, source, *provider->name, canonical.name,
              std::move(context), confidence));
        }
      }
      break;
    case IdentityEntityType::Team:
      for (const auto* provider :
           uniqueMetadata(metadata.teams, &ProviderTeamMetadata::reference,
                          &ProviderTeamMetadata::name)) {
        if (catalog.resolveTeam(provider->reference)) {
          continue;
        }
        for (const auto& [id, canonical] : catalog.teams()) {
          if (canonical.status != CanonicalEntityStatus::Active) {
            continue;
          }
          if (normalizedName(provider->name) ==
              normalizedName(canonical.name)) {
            candidates.push_back(candidate(
                entity_type,
                {provider->reference.provider, provider->reference.id,
                 provider->reference.match_id},
                id, source, provider->name, canonical.name, missingContext(),
                1.0));
          }
        }
      }
      break;
    case IdentityEntityType::Player:
      for (const auto* provider :
           uniqueMetadata(metadata.players,
                          &ProviderPlayerMetadata::reference,
                          &ProviderPlayerMetadata::name)) {
        if (catalog.resolvePlayer(provider->reference)) {
          continue;
        }
        for (const auto& [id, canonical] : catalog.players()) {
          if (canonical.status != CanonicalEntityStatus::Active) {
            continue;
          }
          if (normalizedName(provider->name) ==
              normalizedName(canonical.name)) {
            candidates.push_back(candidate(
                entity_type,
                {provider->reference.provider, provider->reference.id,
                 provider->reference.match_id},
                id, source, provider->name, canonical.name, missingContext(),
                1.0));
          }
        }
      }
      break;
  }
  sortCandidates(candidates);
  return candidates;
}

std::string_view identityEntityTypeName(
    IdentityEntityType entity_type) noexcept {
  switch (entity_type) {
    case IdentityEntityType::Competition:
      return "competition";
    case IdentityEntityType::Season:
      return "season";
    case IdentityEntityType::Team:
      return "team";
    case IdentityEntityType::Player:
      return "player";
  }
  return "unknown";
}

}  // namespace emberdb
