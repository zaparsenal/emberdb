#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "emberdb/identity/canonical_identity.h"

namespace emberdb {

struct ProviderCompetitionMetadata {
  std::string provider;
  std::string id;
  std::string name;
};

struct ProviderTeamMetadata {
  ProviderTeamReference reference;
  std::string name;
};

struct ProviderPlayerMetadata {
  ProviderPlayerReference reference;
  std::string name;
  std::optional<ProviderTeamReference> current_team;
};

struct ProviderMatchMetadata {
  ProviderMatchReference reference;
  std::string competition_id;
  std::optional<std::string> competition_name;
  std::string season_id;
  std::optional<std::string> season_name;
  std::optional<std::chrono::sys_seconds> kickoff;
  ProviderTeamReference home_team;
  ProviderTeamReference away_team;
  std::optional<std::int32_t> home_score;
  std::optional<std::int32_t> away_score;
};

struct ProviderMetadata {
  std::vector<ProviderCompetitionMetadata> competitions;
  std::vector<ProviderTeamMetadata> teams;
  std::vector<ProviderPlayerMetadata> players;
  std::vector<ProviderMatchMetadata> matches;
};

}  // namespace emberdb
