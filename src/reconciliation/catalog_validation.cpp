#include "emberdb/reconciliation/catalog_validation.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace emberdb {
namespace {

struct ValidationInput {
  ProviderIdentityReference reference;
  std::optional<std::string> name;
  std::optional<ProviderCompetitionReference> competition;
  std::optional<ProviderTeamReference> current_team;
  bool operator==(const ValidationInput&) const = default;
};

struct CanonicalSnapshot {
  Identifier id{};
  std::string name;
  CanonicalEntityStatus status{CanonicalEntityStatus::Active};
  std::optional<CanonicalCompetitionId> competition_id;
};

bool blank(std::string_view value) {
  return std::ranges::all_of(value, [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
}

template <typename Reference>
void requireReference(const Reference& reference) {
  if (reference.provider.empty() || reference.id.empty()) {
    throw std::invalid_argument(
        "provider entity metadata requires non-empty provider and ID");
  }
}

template <typename Reference>
void addUnique(std::map<Reference, ValidationInput>& inputs,
               Reference reference, ValidationInput input) {
  requireReference(reference);
  const auto position = inputs.find(reference);
  if (position != inputs.end()) {
    if (position->second == input) {
      return;
    }
    throw std::invalid_argument(
        "provider entity metadata contains conflicting duplicate records");
  }
  inputs.emplace(std::move(reference), std::move(input));
}

std::vector<ValidationInput> validationInputs(
    const ProviderMetadata& metadata, IdentityEntityType entity_type) {
  std::vector<ValidationInput> result;
  switch (entity_type) {
    case IdentityEntityType::Competition: {
      std::map<ProviderCompetitionReference, ValidationInput> unique;
      for (const auto& record : metadata.competitions) {
        if (record.name.empty() || blank(record.name)) {
          throw std::invalid_argument(
              "provider entity metadata name must not be blank");
        }
        addUnique(
            unique, record.reference,
            {{record.reference.provider, record.reference.id, std::nullopt},
             record.name, std::nullopt, std::nullopt});
      }
      for (auto& [unused, input] : unique) {
        static_cast<void>(unused);
        result.push_back(std::move(input));
      }
      break;
    }
    case IdentityEntityType::Season: {
      std::map<ProviderSeasonReference, ValidationInput> unique;
      for (const auto& record : metadata.seasons) {
        requireReference(record.competition);
        addUnique(
            unique, record.reference,
            {{record.reference.provider, record.reference.id, std::nullopt},
             record.name, record.competition, std::nullopt});
      }
      for (auto& [unused, input] : unique) {
        static_cast<void>(unused);
        result.push_back(std::move(input));
      }
      break;
    }
    case IdentityEntityType::Team: {
      std::map<ProviderTeamReference, ValidationInput> unique;
      for (const auto& record : metadata.teams) {
        if (record.name.empty() || blank(record.name)) {
          throw std::invalid_argument(
              "provider entity metadata name must not be blank");
        }
        addUnique(
            unique, record.reference,
            {{record.reference.provider, record.reference.id,
              record.reference.match_id},
             record.name, std::nullopt, std::nullopt});
      }
      for (auto& [unused, input] : unique) {
        static_cast<void>(unused);
        result.push_back(std::move(input));
      }
      break;
    }
    case IdentityEntityType::Player: {
      std::map<ProviderPlayerReference, ValidationInput> unique;
      for (const auto& record : metadata.players) {
        if (record.name.empty() || blank(record.name)) {
          throw std::invalid_argument(
              "provider entity metadata name must not be blank");
        }
        if (record.current_team) {
          requireReference(*record.current_team);
        }
        addUnique(
            unique, record.reference,
            {{record.reference.provider, record.reference.id,
              record.reference.match_id},
             record.name, std::nullopt, record.current_team});
      }
      for (auto& [unused, input] : unique) {
        static_cast<void>(unused);
        result.push_back(std::move(input));
      }
      break;
    }
  }
  return result;
}

std::optional<CanonicalSnapshot> mappedCanonical(
    const CanonicalIdentityCatalog& catalog, IdentityEntityType entity_type,
    const ProviderIdentityReference& reference) {
  switch (entity_type) {
    case IdentityEntityType::Competition:
      if (const auto id =
              catalog.resolveCompetition({reference.provider, reference.id})) {
        const auto* canonical = catalog.competition(*id);
        return CanonicalSnapshot{id->value, canonical->name,
                                 canonical->status, std::nullopt};
      }
      break;
    case IdentityEntityType::Season:
      if (const auto id =
              catalog.resolveSeason({reference.provider, reference.id})) {
        const auto* canonical = catalog.season(*id);
        return CanonicalSnapshot{id->value, canonical->name,
                                 canonical->status,
                                 canonical->competition_id};
      }
      break;
    case IdentityEntityType::Team:
      if (const auto id =
              catalog.resolveTeam({reference.provider, reference.id,
                                   reference.match_id})) {
        const auto* canonical = catalog.team(*id);
        return CanonicalSnapshot{id->value, canonical->name,
                                 canonical->status, std::nullopt};
      }
      break;
    case IdentityEntityType::Player:
      if (const auto id =
              catalog.resolvePlayer({reference.provider, reference.id,
                                     reference.match_id})) {
        const auto* canonical = catalog.player(*id);
        return CanonicalSnapshot{id->value, canonical->name,
                                 canonical->status, std::nullopt};
      }
      break;
  }
  return std::nullopt;
}

std::vector<CanonicalSnapshot> exactCanonicalNames(
    const CanonicalIdentityCatalog& catalog, IdentityEntityType entity_type,
    std::string_view provider_name) {
  std::vector<CanonicalSnapshot> matches;
  const auto normalized_provider_name = normalizeIdentityName(provider_name);
  switch (entity_type) {
    case IdentityEntityType::Competition:
      for (const auto& [id, canonical] : catalog.competitions()) {
        if (normalizeIdentityName(canonical.name) ==
            normalized_provider_name) {
          matches.push_back(
              {id.value, canonical.name, canonical.status, std::nullopt});
        }
      }
      break;
    case IdentityEntityType::Season:
      for (const auto& [id, canonical] : catalog.seasons()) {
        if (normalizeIdentityName(canonical.name) ==
            normalized_provider_name) {
          matches.push_back({id.value, canonical.name, canonical.status,
                             canonical.competition_id});
        }
      }
      break;
    case IdentityEntityType::Team:
      for (const auto& [id, canonical] : catalog.teams()) {
        if (normalizeIdentityName(canonical.name) ==
            normalized_provider_name) {
          matches.push_back(
              {id.value, canonical.name, canonical.status, std::nullopt});
        }
      }
      break;
    case IdentityEntityType::Player:
      for (const auto& [id, canonical] : catalog.players()) {
        if (normalizeIdentityName(canonical.name) ==
            normalized_provider_name) {
          matches.push_back(
              {id.value, canonical.name, canonical.status, std::nullopt});
        }
      }
      break;
  }
  return matches;
}

CatalogValidationContextStatus mappedSeasonContext(
    const ValidationInput& input, const CanonicalSnapshot& mapped,
    const CanonicalIdentityCatalog& catalog) {
  const auto parent =
      catalog.resolveCompetition(*input.competition);
  if (!parent) {
    return CatalogValidationContextStatus::Missing;
  }
  const auto* competition = catalog.competition(*parent);
  if (competition->status != CanonicalEntityStatus::Active) {
    return CatalogValidationContextStatus::Inactive;
  }
  return mapped.competition_id == parent
             ? CatalogValidationContextStatus::Agreeing
             : CatalogValidationContextStatus::Conflicting;
}

CatalogValidationRecord validateInput(
    const ValidationInput& input, IdentityEntityType entity_type,
    const CanonicalIdentityCatalog& catalog) {
  CatalogValidationRecord record;
  record.entity_type = entity_type;
  record.provider_identity = input.reference;
  record.provider_name = input.name;
  const auto missing_name =
      !input.name || input.name->empty() || blank(*input.name);
  const auto mapped =
      mappedCanonical(catalog, entity_type, input.reference);
  if (mapped) {
    record.canonical_ids.push_back(mapped->id);
    record.outcome =
        mapped->status == CanonicalEntityStatus::Active
            ? CatalogValidationOutcome::MappedActive
            : CatalogValidationOutcome::MappedInactive;
    if (!missing_name) {
      record.name_status =
          normalizeIdentityName(*input.name) ==
                  normalizeIdentityName(mapped->name)
              ? ReconciliationStatus::Agreeing
              : ReconciliationStatus::Conflicting;
    }
    if (entity_type == IdentityEntityType::Season) {
      record.context_status =
          mappedSeasonContext(input, *mapped, catalog);
    }
    return record;
  }

  if (missing_name) {
    record.outcome = CatalogValidationOutcome::MissingName;
    if (entity_type == IdentityEntityType::Season) {
      const auto parent =
          catalog.resolveCompetition(*input.competition);
      if (!parent) {
        record.context_status = CatalogValidationContextStatus::Missing;
      } else if (catalog.competition(*parent)->status !=
                 CanonicalEntityStatus::Active) {
        record.context_status = CatalogValidationContextStatus::Inactive;
      } else {
        record.context_status = CatalogValidationContextStatus::Agreeing;
      }
    }
    return record;
  }

  auto exact_matches =
      exactCanonicalNames(catalog, entity_type, *input.name);
  if (entity_type == IdentityEntityType::Season) {
    const auto parent =
        catalog.resolveCompetition(*input.competition);
    if (!parent) {
      record.context_status = CatalogValidationContextStatus::Missing;
    } else if (catalog.competition(*parent)->status !=
               CanonicalEntityStatus::Active) {
      record.context_status = CatalogValidationContextStatus::Inactive;
      std::erase_if(exact_matches, [parent](const auto& match) {
        return match.competition_id != parent;
      });
    } else {
      const auto has_matching_parent =
          std::ranges::any_of(exact_matches, [parent](const auto& match) {
            return match.competition_id == parent;
          });
      record.context_status =
          !exact_matches.empty() && !has_matching_parent
              ? CatalogValidationContextStatus::Conflicting
              : CatalogValidationContextStatus::Agreeing;
      if (!exact_matches.empty() && !has_matching_parent) {
        record.outcome = CatalogValidationOutcome::NoExactMatch;
        record.name_status = ReconciliationStatus::Agreeing;
        for (const auto& match : exact_matches) {
          record.canonical_ids.push_back(match.id);
        }
        return record;
      }
      if (has_matching_parent) {
        std::erase_if(exact_matches, [parent](const auto& match) {
          return match.competition_id != parent;
        });
      }
    }
  }

  std::vector<Identifier> active_ids;
  std::vector<Identifier> inactive_ids;
  for (const auto& match : exact_matches) {
    if (match.status == CanonicalEntityStatus::Active) {
      active_ids.push_back(match.id);
    } else {
      inactive_ids.push_back(match.id);
    }
  }
  if (active_ids.size() == 1U) {
    record.outcome = CatalogValidationOutcome::ExactMatch;
    record.canonical_ids = std::move(active_ids);
    record.name_status = ReconciliationStatus::Agreeing;
  } else if (active_ids.size() > 1U) {
    record.outcome = CatalogValidationOutcome::AmbiguousExactMatch;
    record.canonical_ids = std::move(active_ids);
    record.name_status = ReconciliationStatus::Agreeing;
  } else if (!inactive_ids.empty()) {
    record.outcome = CatalogValidationOutcome::InactiveExactMatch;
    record.canonical_ids = std::move(inactive_ids);
    record.name_status = ReconciliationStatus::Agreeing;
  } else {
    record.outcome = CatalogValidationOutcome::NoExactMatch;
  }
  return record;
}

CatalogValidationSummary summarize(
    const std::vector<CatalogValidationRecord>& records) {
  CatalogValidationSummary summary;
  summary.provider_records = records.size();
  for (const auto& record : records) {
    switch (record.outcome) {
      case CatalogValidationOutcome::MappedActive:
        ++summary.mapped_active;
        break;
      case CatalogValidationOutcome::MappedInactive:
        ++summary.mapped_inactive;
        break;
      case CatalogValidationOutcome::ExactMatch:
        ++summary.unmapped_exact_matches;
        break;
      case CatalogValidationOutcome::AmbiguousExactMatch:
        ++summary.ambiguous_exact_matches;
        break;
      case CatalogValidationOutcome::InactiveExactMatch:
        ++summary.inactive_exact_matches;
        break;
      case CatalogValidationOutcome::NoExactMatch:
        ++summary.no_exact_matches;
        break;
      case CatalogValidationOutcome::MissingName:
        break;
    }
    if (!record.provider_name || record.provider_name->empty() ||
        blank(*record.provider_name)) {
      ++summary.missing_names;
    }
    if ((record.outcome == CatalogValidationOutcome::MappedActive ||
         record.outcome == CatalogValidationOutcome::MappedInactive) &&
        record.name_status == ReconciliationStatus::Conflicting) {
      ++summary.mapped_name_mismatches;
    }
    switch (record.context_status) {
      case CatalogValidationContextStatus::NotApplicable:
      case CatalogValidationContextStatus::Agreeing:
        break;
      case CatalogValidationContextStatus::Missing:
        ++summary.parent_context_missing;
        break;
      case CatalogValidationContextStatus::Conflicting:
        ++summary.parent_context_conflicts;
        break;
      case CatalogValidationContextStatus::Inactive:
        ++summary.parent_context_inactive;
        break;
    }
  }
  return summary;
}

}  // namespace

CatalogValidationReport validateCatalogMetadata(
    const ProviderMetadata& metadata, IdentityEntityType entity_type,
    const CanonicalIdentityCatalog& catalog) {
  CatalogValidationReport report;
  report.entity_type = entity_type;
  for (const auto& input : validationInputs(metadata, entity_type)) {
    report.records.push_back(validateInput(input, entity_type, catalog));
  }
  std::ranges::sort(report.records, [](const auto& left, const auto& right) {
    return std::tie(left.provider_identity.provider,
                    left.provider_identity.id,
                    left.provider_identity.match_id) <
           std::tie(right.provider_identity.provider,
                    right.provider_identity.id,
                    right.provider_identity.match_id);
  });
  report.summary = summarize(report.records);
  return report;
}

std::string_view catalogValidationOutcomeName(
    CatalogValidationOutcome outcome) noexcept {
  switch (outcome) {
    case CatalogValidationOutcome::MappedActive:
      return "mapped_active";
    case CatalogValidationOutcome::MappedInactive:
      return "mapped_inactive";
    case CatalogValidationOutcome::ExactMatch:
      return "exact_match";
    case CatalogValidationOutcome::AmbiguousExactMatch:
      return "ambiguous_exact_match";
    case CatalogValidationOutcome::InactiveExactMatch:
      return "inactive_exact_match";
    case CatalogValidationOutcome::NoExactMatch:
      return "no_exact_match";
    case CatalogValidationOutcome::MissingName:
      return "missing_name";
  }
  return "unknown";
}

std::string_view catalogValidationContextStatusName(
    CatalogValidationContextStatus status) noexcept {
  switch (status) {
    case CatalogValidationContextStatus::NotApplicable:
      return "not_applicable";
    case CatalogValidationContextStatus::Missing:
      return "missing";
    case CatalogValidationContextStatus::Agreeing:
      return "agreeing";
    case CatalogValidationContextStatus::Conflicting:
      return "conflicting";
    case CatalogValidationContextStatus::Inactive:
      return "inactive";
  }
  return "unknown";
}

}  // namespace emberdb
