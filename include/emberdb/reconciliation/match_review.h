#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/reconciliation/match_reconciliation.h"

namespace emberdb {

enum class MatchCandidateStatus { Unresolved, Accepted, Rejected };

struct MatchCandidateRecord {
  std::uint64_t id{};
  MatchReconciliation reconciliation;
  MatchCandidateStatus status{MatchCandidateStatus::Unresolved};
  std::optional<CanonicalMatchId> accepted_match_id;
  std::optional<std::string> rejection_reason;
};

class MatchReviewStore {
 public:
  MatchReviewStore() = default;
  explicit MatchReviewStore(CanonicalIdentityCatalog catalog);

  [[nodiscard]] CanonicalIdentityCatalog& catalog() noexcept;
  [[nodiscard]] const CanonicalIdentityCatalog& catalog() const noexcept;

  [[nodiscard]] std::vector<std::uint64_t> addCandidates(
      const std::vector<MatchReconciliation>& candidates);
  [[nodiscard]] const MatchCandidateRecord* candidate(std::uint64_t id) const;
  [[nodiscard]] std::vector<const MatchCandidateRecord*> candidates(
      std::optional<MatchCandidateStatus> status = std::nullopt) const;

  void accept(std::uint64_t candidate_id, CanonicalMatchId canonical_match_id);
  void reject(std::uint64_t candidate_id, std::string reason);

 private:
  [[nodiscard]] MatchCandidateRecord& requireCandidate(std::uint64_t id);

  CanonicalIdentityCatalog catalog_;
  std::vector<MatchCandidateRecord> candidates_;
  std::uint64_t next_candidate_id_{1};
};

[[nodiscard]] std::string_view matchCandidateStatusName(
    MatchCandidateStatus status) noexcept;

}  // namespace emberdb
