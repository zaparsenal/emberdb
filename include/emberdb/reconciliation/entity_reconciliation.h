#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/ingestion/provider_metadata.h"
#include "emberdb/reconciliation/match_reconciliation.h"

namespace emberdb {

enum class IdentityEntityType { Competition, Season, Team, Player };

struct ProviderIdentityReference {
  std::string provider;
  std::string id;
  std::optional<std::string> match_id;
  bool operator==(const ProviderIdentityReference&) const = default;
};

struct EntityFieldEvidence {
  ReconciliationStatus status{ReconciliationStatus::Missing};
  std::optional<std::string> provider_value;
  std::optional<std::string> canonical_value;
};

struct EntityReconciliation {
  IdentityEntityType entity_type{IdentityEntityType::Team};
  ProviderIdentityReference provider_identity;
  Identifier canonical_id{};
  std::string source;
  EntityFieldEvidence name;
  EntityFieldEvidence context;
  double confidence{};
};

[[nodiscard]] std::vector<EntityReconciliation> findEntityCandidates(
    const ProviderMetadata& metadata, IdentityEntityType entity_type,
    const CanonicalIdentityCatalog& catalog, std::string source);

[[nodiscard]] std::string_view identityEntityTypeName(
    IdentityEntityType entity_type) noexcept;

}  // namespace emberdb
