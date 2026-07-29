#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "emberdb/common/coordinate_normalization.h"
#include "emberdb/identity/canonical_identity.h"
#include "emberdb/reconciliation/match_review.h"

namespace emberdb::cli {

enum class Command {
  Import,
  Query,
  ReconcileGenerate,
  ReconcileList,
  ReconcileInspect,
  ReconcileAccept,
  ReconcileReject
};

struct Options {
  Command command{Command::Import};
  std::string provider;
  Identifier match_id{};
  bool has_match_id{};
  std::filesystem::path input;
  std::filesystem::path output;
  std::filesystem::path database;
  std::size_t limit{};
  bool has_limit{};
  std::vector<std::string> filters;
  std::string projection;
  std::string group_by;
  std::vector<std::string> aggregates;
  std::optional<AttackingDirection> home_first_half_direction;
  std::filesystem::path review;
  std::string left_provider;
  std::filesystem::path left_input;
  std::string right_provider;
  std::filesystem::path right_input;
  std::uint64_t candidate_id{};
  bool has_candidate_id{};
  Identifier canonical_match_id{};
  bool has_canonical_match_id{};
  std::optional<MatchCandidateStatus> candidate_status;
  std::string actor;
  std::string source;
  std::string reason;
};

}  // namespace emberdb::cli
