#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/reconciliation/entity_reconciliation.h"

namespace emberdb {

enum class CatalogValidationOutcome {
  MappedActive,
  MappedInactive,
  ExactMatch,
  AmbiguousExactMatch,
  InactiveExactMatch,
  NoExactMatch,
  MissingName
};

enum class CatalogValidationContextStatus {
  NotApplicable,
  Missing,
  Agreeing,
  Conflicting,
  Inactive
};

struct CatalogValidationRecord {
  IdentityEntityType entity_type{IdentityEntityType::Team};
  ProviderIdentityReference provider_identity;
  std::optional<std::string> provider_name;
  CatalogValidationOutcome outcome{CatalogValidationOutcome::NoExactMatch};
  std::vector<Identifier> canonical_ids;
  ReconciliationStatus name_status{ReconciliationStatus::Missing};
  CatalogValidationContextStatus context_status{
      CatalogValidationContextStatus::NotApplicable};
};

struct CatalogValidationSummary {
  std::size_t provider_records{};
  std::size_t mapped_active{};
  std::size_t mapped_inactive{};
  std::size_t unmapped_exact_matches{};
  std::size_t ambiguous_exact_matches{};
  std::size_t inactive_exact_matches{};
  std::size_t no_exact_matches{};
  std::size_t missing_names{};
  std::size_t mapped_name_mismatches{};
  std::size_t parent_context_missing{};
  std::size_t parent_context_conflicts{};
  std::size_t parent_context_inactive{};
};

struct CatalogValidationReport {
  IdentityEntityType entity_type{IdentityEntityType::Team};
  CatalogValidationSummary summary;
  std::vector<CatalogValidationRecord> records;
};

[[nodiscard]] CatalogValidationReport validateCatalogMetadata(
    const ProviderMetadata& metadata, IdentityEntityType entity_type,
    const CanonicalIdentityCatalog& catalog);

[[nodiscard]] std::string_view catalogValidationOutcomeName(
    CatalogValidationOutcome outcome) noexcept;

[[nodiscard]] std::string_view catalogValidationContextStatusName(
    CatalogValidationContextStatus status) noexcept;

}  // namespace emberdb
