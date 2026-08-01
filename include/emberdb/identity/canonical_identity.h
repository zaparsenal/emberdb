#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

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

struct CanonicalCompetitionId {
  Identifier value{};
  auto operator<=>(const CanonicalCompetitionId&) const = default;
};

struct CanonicalSeasonId {
  Identifier value{};
  auto operator<=>(const CanonicalSeasonId&) const = default;
};

enum class CanonicalEntityStatus { Active, Deprecated, Merged };

struct CanonicalCompetition {
  CanonicalCompetition(CanonicalCompetitionId id, std::string name)
      : id(id), name(std::move(name)) {}

  CanonicalCompetitionId id;
  std::string name;
  CanonicalEntityStatus status{CanonicalEntityStatus::Active};
  std::optional<CanonicalCompetitionId> merged_into;
};

struct CanonicalSeason {
  CanonicalSeason(CanonicalSeasonId id,
                  CanonicalCompetitionId competition_id, std::string name)
      : id(id),
        competition_id(competition_id),
        name(std::move(name)) {}

  CanonicalSeasonId id;
  CanonicalCompetitionId competition_id;
  std::string name;
  CanonicalEntityStatus status{CanonicalEntityStatus::Active};
  std::optional<CanonicalSeasonId> merged_into;
};

struct CanonicalTeam {
  CanonicalTeam(CanonicalTeamId id, std::string name)
      : id(id), name(std::move(name)) {}

  CanonicalTeamId id;
  std::string name;
  CanonicalEntityStatus status{CanonicalEntityStatus::Active};
  std::optional<CanonicalTeamId> merged_into;
};

struct CanonicalPlayer {
  CanonicalPlayer(CanonicalPlayerId id, std::string name)
      : id(id), name(std::move(name)) {}

  CanonicalPlayerId id;
  std::string name;
  CanonicalEntityStatus status{CanonicalEntityStatus::Active};
  std::optional<CanonicalPlayerId> merged_into;
};

struct LegacyCanonicalMatchAncestry {
  std::string competition;
  std::string season;
  bool operator==(const LegacyCanonicalMatchAncestry&) const = default;
};

struct CanonicalMatch {
  CanonicalMatch(CanonicalMatchId id, CanonicalSeasonId season_id,
                 std::optional<std::chrono::sys_seconds> kickoff,
                 CanonicalTeamId home_team_id,
                 CanonicalTeamId away_team_id,
                 std::optional<std::int32_t> home_score,
                 std::optional<std::int32_t> away_score)
      : id(id),
        season_id(season_id),
        kickoff(kickoff),
        home_team_id(home_team_id),
        away_team_id(away_team_id),
        home_score(home_score),
        away_score(away_score) {}

  [[nodiscard]] static CanonicalMatch legacy(
      CanonicalMatchId id, LegacyCanonicalMatchAncestry ancestry,
      std::optional<std::chrono::sys_seconds> kickoff,
      CanonicalTeamId home_team_id, CanonicalTeamId away_team_id,
      std::optional<std::int32_t> home_score,
      std::optional<std::int32_t> away_score);

  CanonicalMatchId id;
  std::optional<CanonicalSeasonId> season_id;
  std::optional<LegacyCanonicalMatchAncestry> legacy_ancestry;
  std::optional<std::chrono::sys_seconds> kickoff;
  CanonicalTeamId home_team_id;
  CanonicalTeamId away_team_id;
  std::optional<std::int32_t> home_score;
  std::optional<std::int32_t> away_score;

 private:
  CanonicalMatch(CanonicalMatchId id, LegacyCanonicalMatchAncestry ancestry,
                 std::optional<std::chrono::sys_seconds> kickoff,
                 CanonicalTeamId home_team_id,
                 CanonicalTeamId away_team_id,
                 std::optional<std::int32_t> home_score,
                 std::optional<std::int32_t> away_score);
};

struct CanonicalMatchLabels {
  std::string competition;
  std::string season;
};

struct ProviderMatchReference {
  std::string provider;
  std::string id;
  bool operator==(const ProviderMatchReference&) const = default;
  bool operator<(const ProviderMatchReference& other) const {
    return std::tie(provider, id) < std::tie(other.provider, other.id);
  }
};

struct ProviderCompetitionReference {
  std::string provider;
  std::string id;
  bool operator==(const ProviderCompetitionReference&) const = default;
  bool operator<(const ProviderCompetitionReference& other) const {
    return std::tie(provider, id) < std::tie(other.provider, other.id);
  }
};

struct ProviderSeasonReference {
  std::string provider;
  std::string id;
  bool operator==(const ProviderSeasonReference&) const = default;
  bool operator<(const ProviderSeasonReference& other) const {
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
  void addCompetition(CanonicalCompetition competition);
  void addSeason(CanonicalSeason season);
  void addTeam(CanonicalTeam team);
  void addPlayer(CanonicalPlayer player);
  void addMatch(CanonicalMatch match);
  void restoreLegacyMatch(CanonicalMatch match);

  void renameCompetition(CanonicalCompetitionId competition,
                         std::string name);
  void renameSeason(CanonicalSeasonId season, std::string name);
  void renameTeam(CanonicalTeamId team, std::string name);
  void renamePlayer(CanonicalPlayerId player, std::string name);
  void deprecateCompetition(CanonicalCompetitionId competition);
  void deprecateSeason(CanonicalSeasonId season);
  void deprecateTeam(CanonicalTeamId team);
  void deprecatePlayer(CanonicalPlayerId player);
  void mergeCompetition(CanonicalCompetitionId source,
                        CanonicalCompetitionId target);
  void mergeSeason(CanonicalSeasonId source, CanonicalSeasonId target);
  void mergeTeam(CanonicalTeamId source, CanonicalTeamId target);
  void mergePlayer(CanonicalPlayerId source, CanonicalPlayerId target);

  void mapCompetition(ProviderCompetitionReference provider_competition,
                      CanonicalCompetitionId canonical_competition);
  void mapSeason(ProviderSeasonReference provider_season,
                 CanonicalSeasonId canonical_season);
  void mapTeam(ProviderTeamReference provider_team, CanonicalTeamId canonical_team);
  void mapPlayer(ProviderPlayerReference provider_player,
                 CanonicalPlayerId canonical_player);
  void mapMatch(ProviderMatchReference provider_match, CanonicalMatchId canonical_match);
  void mapMetricaTeams(std::string provider_match_id, CanonicalTeamId home_team,
                       CanonicalTeamId away_team);

  [[nodiscard]] const CanonicalCompetition* competition(
      CanonicalCompetitionId id) const;
  [[nodiscard]] const CanonicalSeason* season(CanonicalSeasonId id) const;
  [[nodiscard]] const CanonicalTeam* team(CanonicalTeamId id) const;
  [[nodiscard]] const CanonicalPlayer* player(CanonicalPlayerId id) const;
  [[nodiscard]] const CanonicalMatch* match(CanonicalMatchId id) const;
  [[nodiscard]] std::optional<CanonicalMatchLabels> matchLabels(
      CanonicalMatchId id) const;

  [[nodiscard]] std::optional<CanonicalCompetitionId> resolveCompetition(
      const ProviderCompetitionReference& provider_competition) const;
  [[nodiscard]] std::optional<CanonicalSeasonId> resolveSeason(
      const ProviderSeasonReference& provider_season) const;
  [[nodiscard]] std::optional<CanonicalTeamId> resolveTeam(
      const ProviderTeamReference& provider_team) const;
  [[nodiscard]] std::optional<CanonicalPlayerId> resolvePlayer(
      const ProviderPlayerReference& provider_player) const;
  [[nodiscard]] std::optional<CanonicalMatchId> resolveMatch(
      const ProviderMatchReference& provider_match) const;
  [[nodiscard]] CanonicalEventIdentity resolveEvent(
      const FootballEvent& event) const;

  [[nodiscard]] const std::map<CanonicalCompetitionId, CanonicalCompetition>&
  competitions() const noexcept;
  [[nodiscard]] const std::map<CanonicalSeasonId, CanonicalSeason>& seasons()
      const noexcept;
  [[nodiscard]] const std::map<CanonicalTeamId, CanonicalTeam>& teams() const
      noexcept;
  [[nodiscard]] const std::map<CanonicalPlayerId, CanonicalPlayer>& players() const
      noexcept;
  [[nodiscard]] const std::map<CanonicalMatchId, CanonicalMatch>& matches() const
      noexcept;
  [[nodiscard]] const std::map<ProviderCompetitionReference,
                               CanonicalCompetitionId>&
  competitionMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderSeasonReference, CanonicalSeasonId>&
  seasonMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderTeamReference, CanonicalTeamId>&
  teamMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderPlayerReference, CanonicalPlayerId>&
  playerMappings() const noexcept;
  [[nodiscard]] const std::map<ProviderMatchReference, CanonicalMatchId>&
  matchMappings() const noexcept;

 private:
  std::map<CanonicalCompetitionId, CanonicalCompetition> competitions_;
  std::map<CanonicalSeasonId, CanonicalSeason> seasons_;
  std::map<CanonicalTeamId, CanonicalTeam> teams_;
  std::map<CanonicalPlayerId, CanonicalPlayer> players_;
  std::map<CanonicalMatchId, CanonicalMatch> matches_;
  std::map<ProviderCompetitionReference, CanonicalCompetitionId>
      competition_mappings_;
  std::map<ProviderSeasonReference, CanonicalSeasonId> season_mappings_;
  std::map<ProviderTeamReference, CanonicalTeamId> team_mappings_;
  std::map<ProviderPlayerReference, CanonicalPlayerId> player_mappings_;
  std::map<ProviderMatchReference, CanonicalMatchId> match_mappings_;
};

[[nodiscard]] std::string_view canonicalEntityStatusName(
    CanonicalEntityStatus status) noexcept;

}  // namespace emberdb
