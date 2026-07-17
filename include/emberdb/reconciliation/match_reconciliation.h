#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "emberdb/identity/canonical_identity.h"
#include "emberdb/ingestion/provider_metadata.h"

namespace emberdb {

enum class ReconciliationStatus { Missing, Agreeing, Conflicting, Uncertain };

struct MatchFieldEvidence {
  ReconciliationStatus status{ReconciliationStatus::Missing};
  std::string left_source;
  std::string right_source;
  std::optional<std::string> left_value;
  std::optional<std::string> right_value;
  std::optional<std::string> canonical_value;
};

struct MatchReconciliationOptions {
  std::chrono::seconds kickoff_tolerance{std::chrono::minutes{5}};
  std::chrono::seconds uncertain_kickoff_tolerance{std::chrono::hours{24}};
  double minimum_confidence{0.70};
};

struct MatchReconciliation {
  ProviderMatchReference left_match;
  ProviderMatchReference right_match;
  MatchFieldEvidence competition;
  MatchFieldEvidence season;
  MatchFieldEvidence kickoff;
  MatchFieldEvidence home_team;
  MatchFieldEvidence away_team;
  MatchFieldEvidence score;
  double confidence{};
  bool is_candidate{};
};

[[nodiscard]] MatchReconciliation reconcileMatches(
    const ProviderMatchMetadata& left, const ProviderMatchMetadata& right,
    const CanonicalIdentityCatalog& catalog,
    const MatchReconciliationOptions& options = {});

[[nodiscard]] std::vector<MatchReconciliation> findMatchCandidates(
    const std::vector<ProviderMatchMetadata>& left_matches,
    const std::vector<ProviderMatchMetadata>& right_matches,
    const CanonicalIdentityCatalog& catalog,
    const MatchReconciliationOptions& options = {});

}  // namespace emberdb
