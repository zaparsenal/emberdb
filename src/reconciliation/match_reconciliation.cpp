#include "emberdb/reconciliation/match_reconciliation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace emberdb {
namespace {

constexpr double kHomeTeamWeight = 0.25;
constexpr double kAwayTeamWeight = 0.25;
constexpr double kKickoffWeight = 0.20;
constexpr double kScoreWeight = 0.15;
constexpr double kCompetitionWeight = 0.10;
constexpr double kSeasonWeight = 0.05;

void validateOptions(const MatchReconciliationOptions& options) {
  if (options.kickoff_tolerance.count() < 0 ||
      options.uncertain_kickoff_tolerance < options.kickoff_tolerance) {
    throw std::invalid_argument(
        "kickoff tolerances must be non-negative and ordered");
  }
  if (!std::isfinite(options.minimum_confidence) ||
      options.minimum_confidence < 0.0 || options.minimum_confidence > 1.0) {
    throw std::invalid_argument("minimum match confidence must be between 0 and 1");
  }
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

MatchFieldEvidence evidence(const ProviderMatchMetadata& left,
                            const ProviderMatchMetadata& right) {
  return {ReconciliationStatus::Missing, left.reference.provider,
          right.reference.provider, std::nullopt, std::nullopt, std::nullopt};
}

MatchFieldEvidence namedIdentityEvidence(
    const ProviderMatchMetadata& left, const ProviderMatchMetadata& right,
    const std::string& left_id, const std::optional<std::string>& left_name,
    const std::string& right_id, const std::optional<std::string>& right_name) {
  auto result = evidence(left, right);
  result.left_value = left_name ? *left_name : left_id;
  result.right_value = right_name ? *right_name : right_id;
  if (left.reference.provider == right.reference.provider) {
    result.status = left_id == right_id ? ReconciliationStatus::Agreeing
                                        : ReconciliationStatus::Conflicting;
    if (result.status == ReconciliationStatus::Agreeing) {
      result.canonical_value = left_id;
    }
    return result;
  }
  if (!left_name || !right_name) {
    result.status = ReconciliationStatus::Missing;
    return result;
  }
  if (normalizedName(*left_name) == normalizedName(*right_name)) {
    result.status = ReconciliationStatus::Agreeing;
    result.canonical_value = normalizedName(*left_name);
  } else {
    result.status = ReconciliationStatus::Uncertain;
  }
  return result;
}

std::string teamReferenceValue(const ProviderTeamReference& reference) {
  if (reference.match_id) {
    return reference.id + "@" + *reference.match_id;
  }
  return reference.id;
}

MatchFieldEvidence teamEvidence(const ProviderMatchMetadata& left,
                                const ProviderMatchMetadata& right,
                                const ProviderTeamReference& left_team,
                                const ProviderTeamReference& right_team,
                                const CanonicalIdentityCatalog& catalog) {
  auto result = evidence(left, right);
  result.left_value = teamReferenceValue(left_team);
  result.right_value = teamReferenceValue(right_team);
  const auto left_id = catalog.resolveTeam(left_team);
  const auto right_id = catalog.resolveTeam(right_team);
  if (!left_id || !right_id) {
    result.status = ReconciliationStatus::Missing;
  } else if (*left_id == *right_id) {
    result.status = ReconciliationStatus::Agreeing;
    result.canonical_value = std::to_string(left_id->value);
  } else {
    result.status = ReconciliationStatus::Conflicting;
  }
  return result;
}

MatchFieldEvidence kickoffEvidence(const ProviderMatchMetadata& left,
                                   const ProviderMatchMetadata& right,
                                   const MatchReconciliationOptions& options) {
  auto result = evidence(left, right);
  if (left.kickoff) {
    result.left_value = std::to_string(left.kickoff->time_since_epoch().count());
  }
  if (right.kickoff) {
    result.right_value = std::to_string(right.kickoff->time_since_epoch().count());
  }
  if (!left.kickoff || !right.kickoff) {
    result.status = ReconciliationStatus::Missing;
    return result;
  }
  const auto difference = *left.kickoff - *right.kickoff;
  const auto absolute_difference =
      difference < std::chrono::seconds::zero() ? -difference : difference;
  if (absolute_difference <= options.kickoff_tolerance) {
    result.status = ReconciliationStatus::Agreeing;
  } else if (absolute_difference <= options.uncertain_kickoff_tolerance) {
    result.status = ReconciliationStatus::Uncertain;
  } else {
    result.status = ReconciliationStatus::Conflicting;
  }
  return result;
}

std::optional<std::string> scoreValue(const ProviderMatchMetadata& match) {
  if (!match.home_score || !match.away_score) {
    return std::nullopt;
  }
  return std::to_string(*match.home_score) + "-" +
         std::to_string(*match.away_score);
}

MatchFieldEvidence scoreEvidence(const ProviderMatchMetadata& left,
                                 const ProviderMatchMetadata& right) {
  auto result = evidence(left, right);
  result.left_value = scoreValue(left);
  result.right_value = scoreValue(right);
  if (!result.left_value || !result.right_value) {
    result.status = ReconciliationStatus::Missing;
  } else if (result.left_value == result.right_value) {
    result.status = ReconciliationStatus::Agreeing;
    result.canonical_value = result.left_value;
  } else {
    result.status = ReconciliationStatus::Conflicting;
  }
  return result;
}

double statusFactor(ReconciliationStatus status) {
  switch (status) {
    case ReconciliationStatus::Agreeing:
      return 1.0;
    case ReconciliationStatus::Uncertain:
      return 0.5;
    case ReconciliationStatus::Missing:
    case ReconciliationStatus::Conflicting:
      return 0.0;
  }
  return 0.0;
}

bool isHardConflict(const MatchReconciliation& result) {
  return result.home_team.status == ReconciliationStatus::Conflicting ||
         result.away_team.status == ReconciliationStatus::Conflicting ||
         result.kickoff.status == ReconciliationStatus::Conflicting ||
         result.score.status == ReconciliationStatus::Conflicting;
}

}  // namespace

MatchReconciliation reconcileMatches(
    const ProviderMatchMetadata& left, const ProviderMatchMetadata& right,
    const CanonicalIdentityCatalog& catalog,
    const MatchReconciliationOptions& options) {
  validateOptions(options);
  MatchReconciliation result;
  result.left_match = left.reference;
  result.right_match = right.reference;
  result.competition = namedIdentityEvidence(
      left, right, left.competition_id, left.competition_name,
      right.competition_id, right.competition_name);
  result.season = namedIdentityEvidence(left, right, left.season_id, left.season_name,
                                        right.season_id, right.season_name);
  result.kickoff = kickoffEvidence(left, right, options);
  result.home_team =
      teamEvidence(left, right, left.home_team, right.home_team, catalog);
  result.away_team =
      teamEvidence(left, right, left.away_team, right.away_team, catalog);
  result.score = scoreEvidence(left, right);
  result.confidence =
      kHomeTeamWeight * statusFactor(result.home_team.status) +
      kAwayTeamWeight * statusFactor(result.away_team.status) +
      kKickoffWeight * statusFactor(result.kickoff.status) +
      kScoreWeight * statusFactor(result.score.status) +
      kCompetitionWeight * statusFactor(result.competition.status) +
      kSeasonWeight * statusFactor(result.season.status);
  result.is_candidate =
      result.home_team.status == ReconciliationStatus::Agreeing &&
      result.away_team.status == ReconciliationStatus::Agreeing &&
      !isHardConflict(result) && result.confidence >= options.minimum_confidence;
  return result;
}

std::vector<MatchReconciliation> findMatchCandidates(
    const std::vector<ProviderMatchMetadata>& left_matches,
    const std::vector<ProviderMatchMetadata>& right_matches,
    const CanonicalIdentityCatalog& catalog,
    const MatchReconciliationOptions& options) {
  validateOptions(options);
  std::vector<MatchReconciliation> candidates;
  for (const auto& left : left_matches) {
    for (const auto& right : right_matches) {
      if (left.reference == right.reference) {
        continue;
      }
      auto result = reconcileMatches(left, right, catalog, options);
      if (result.is_candidate) {
        candidates.push_back(std::move(result));
      }
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const MatchReconciliation& first,
                      const MatchReconciliation& second) {
                     if (first.confidence != second.confidence) {
                       return first.confidence > second.confidence;
                     }
                     return std::tie(first.left_match.provider, first.left_match.id,
                                     first.right_match.provider, first.right_match.id) <
                            std::tie(second.left_match.provider, second.left_match.id,
                                     second.right_match.provider, second.right_match.id);
                   });
  return candidates;
}

}  // namespace emberdb
