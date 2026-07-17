#include "emberdb/reconciliation/match_review.h"

#include <algorithm>
#include <cctype>
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

bool blank(std::string_view value) {
  return std::ranges::all_of(value, [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
  });
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

CanonicalIdentityCatalog& MatchReviewStore::catalog() noexcept { return catalog_; }

const CanonicalIdentityCatalog& MatchReviewStore::catalog() const noexcept {
  return catalog_;
}

std::vector<std::uint64_t> MatchReviewStore::addCandidates(
    const std::vector<MatchReconciliation>& candidates) {
  std::vector<std::uint64_t> ids;
  ids.reserve(candidates.size());
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
    const auto id = next_candidate_id_++;
    candidates_.push_back(
        {id, reconciliation, MatchCandidateStatus::Unresolved, std::nullopt,
         std::nullopt});
    ids.push_back(id);
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

void MatchReviewStore::accept(std::uint64_t candidate_id,
                              CanonicalMatchId canonical_match_id) {
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
  catalog_.mapMatch(record.reconciliation.left_match, canonical_match_id);
  catalog_.mapMatch(record.reconciliation.right_match, canonical_match_id);
  record.status = MatchCandidateStatus::Accepted;
  record.accepted_match_id = canonical_match_id;
}

void MatchReviewStore::reject(std::uint64_t candidate_id, std::string reason) {
  if (reason.empty() || blank(reason)) {
    throw std::invalid_argument("candidate rejection reason must not be blank");
  }
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
  record.status = MatchCandidateStatus::Rejected;
  record.rejection_reason = std::move(reason);
}

MatchCandidateRecord& MatchReviewStore::requireCandidate(std::uint64_t id) {
  const auto position = std::ranges::find(candidates_, id,
                                          &MatchCandidateRecord::id);
  if (position == candidates_.end()) {
    throw std::invalid_argument("unknown match candidate " + std::to_string(id));
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

}  // namespace emberdb
