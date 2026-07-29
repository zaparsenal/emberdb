#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>

#include "emberdb/common/football_event.h"

namespace emberdb {

struct CanonicalTeamId {
  Identifier value{};
  auto operator<=>(const CanonicalTeamId&) const = default;
};

struct CanonicalPlayerId {
  Identifier value{};
  auto operator<=>(const CanonicalPlayerId&) const = default;
};

struct CanonicalMatchId {
  Identifier value{};
  auto operator<=>(const CanonicalMatchId&) const = default;
};

struct CanonicalTeam {
  CanonicalTeamId id;
  std::string name;
};

struct CanonicalPlayer {
  CanonicalPlayerId id;
  std::string name;
};

struct CanonicalMatch {
  CanonicalMatchId id;
  std::string competition;
  std::string season;
  std::optional<std::chrono::sys_seconds> kickoff;
  CanonicalTeamId home_team_id;
  CanonicalTeamId away_team_id;
  std::optional<std::int32_t> home_score;
  std::optional<std::int32_t> away_score;
};

struct ProviderMatchReference {
  std::string provider;
  std::string id;
  bool operator==(const ProviderMatchReference&) const = default;
  bool operator<(const ProviderMatchReference& other) const {
    return std::tie(provider, id) < std::tie(other.provider, other.id);
  }
};

// match_id scopes anonymous or match-local provider identifiers such as Metrica's
// repeating Home, Away, and PlayerN labels. Stable provider IDs leave it null.
struct ProviderTeamReference {
  std::string provider;
  std::string id;
  std::optional<std::string> match_id;
  bool operator==(const ProviderTeamReference&) const = default;
  bool operator<(const ProviderTeamReference& other) const {
    return std::tie(provider, id, match_id) <
           std::tie(other.provider, other.id, other.match_id);
  }
};

struct ProviderPlayerReference {
  std::string provider;
  std::string id;
  std::optional<std::string> match_id;
  bool operator==(const ProviderPlayerReference&) const = default;
  bool operator<(const ProviderPlayerReference& other) const {
    return std::tie(provider, id, match_id) <
           std::tie(other.provider, other.id, other.match_id);
  }
};

struct CanonicalEventIdentity {
  std::optional<CanonicalMatchId> match_id;
  std::optional<CanonicalTeamId> team_id;
  std::optional<CanonicalPlayerId> player_id;
};

class CanonicalIdentityCatalog {
 public:
  void addTeam(CanonicalTeam team);
  void addPlayer(CanonicalPlayer player);
  void addMatch(CanonicalMatch match);

  void mapTeam(ProviderTeamReference provider_team, CanonicalTeamId canonical_team);
  void mapPlayer(ProviderPlayerReference provider_player,
                 CanonicalPlayerId canonical_player);
  void mapMatch(ProviderMatchReference provider_match, CanonicalMatchId canonical_match);
  void mapMetricaTeams(std::string provider_match_id, CanonicalTeamId home_team,
                       CanonicalTeamId away_team);

  [[nodiscard]] const CanonicalTeam* team(CanonicalTeamId id) const;
  [[nodiscard]] const CanonicalPlayer* player(CanonicalPlayerId id) const;
  [[nodiscard]] const CanonicalMatch* match(CanonicalMatchId id) const;

  [[nodiscard]] std::optional<CanonicalTeamId> resolveTeam(
      const ProviderTeamReference& provider_team) const;
  [[nodiscard]] std::optional<CanonicalPlayerId> resolvePlayer(
      const ProviderPlayerReference& provider_player) const;
  [[nodiscard]] std::optional<CanonicalMatchId> resolveMatch(
      const ProviderMatchReference& provider_match) const;
  [[nodiscard]] CanonicalEventIdentity resolveEvent(
      const FootballEvent& event) const;

  [[nodiscard]] const std::map<CanonicalTeamId, CanonicalTeam>& teams() const
      noexcept;
  [[nodiscard]] const std::map<CanonicalPlayerId, CanonicalPlayer>& players() const
      noexcept;
  [[nodiscard]] const std::map<CanonicalMatchId, CanonicalMatch>& matches() const
      noexcept;
  [[nodiscard]] const std::map<ProviderTeamReference, CanonicalTeamId>&
  teamMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderPlayerReference, CanonicalPlayerId>&
  playerMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderMatchReference, CanonicalMatchId>&
  matchMappings() const noexcept;

 private:
  std::map<CanonicalTeamId, CanonicalTeam> teams_;
  std::map<CanonicalPlayerId, CanonicalPlayer> players_;
  std::map<CanonicalMatchId, CanonicalMatch> matches_;
  std::map<ProviderTeamReference, CanonicalTeamId> team_mappings_;
  std::map<ProviderPlayerReference, CanonicalPlayerId> player_mappings_;
  std::map<ProviderMatchReference, CanonicalMatchId> match_mappings_;
};

}  // namespace emberdb
