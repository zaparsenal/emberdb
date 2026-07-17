#include "emberdb/identity/canonical_identity.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace emberdb {
namespace {

template <typename Id>
void requirePositive(Id id, const char* name) {
  if (id.value <= 0) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
}

void requireProviderReference(const std::string& provider, const std::string& id) {
  if (provider.empty() || id.empty()) {
    throw std::invalid_argument("provider mappings require non-empty provider and ID");
  }
}

template <typename Reference, typename CanonicalId>
void addMapping(std::map<Reference, CanonicalId>& mappings, Reference reference,
                CanonicalId canonical_id) {
  const auto [position, inserted] =
      mappings.emplace(std::move(reference), canonical_id);
  if (!inserted && position->second != canonical_id) {
    throw std::invalid_argument(
        "provider identity is already mapped to a different canonical ID");
  }
}

template <typename Reference, typename CanonicalId>
std::optional<CanonicalId> resolve(
    const std::map<Reference, CanonicalId>& mappings,
    const Reference& reference) {
  const auto position = mappings.find(reference);
  if (position == mappings.end()) {
    return std::nullopt;
  }
  return position->second;
}

}  // namespace

void CanonicalIdentityCatalog::addTeam(CanonicalTeam team) {
  requirePositive(team.id, "canonical team ID");
  if (team.name.empty()) {
    throw std::invalid_argument("canonical team name must not be empty");
  }
  if (!teams_.emplace(team.id, std::move(team)).second) {
    throw std::invalid_argument("duplicate canonical team ID");
  }
}

void CanonicalIdentityCatalog::addPlayer(CanonicalPlayer player) {
  requirePositive(player.id, "canonical player ID");
  if (player.name.empty()) {
    throw std::invalid_argument("canonical player name must not be empty");
  }
  if (!players_.emplace(player.id, std::move(player)).second) {
    throw std::invalid_argument("duplicate canonical player ID");
  }
}

void CanonicalIdentityCatalog::addMatch(CanonicalMatch match) {
  requirePositive(match.id, "canonical match ID");
  if (match.competition.empty() || match.season.empty()) {
    throw std::invalid_argument(
        "canonical matches require competition and season");
  }
  if (!teams_.contains(match.home_team_id) ||
      !teams_.contains(match.away_team_id)) {
    throw std::invalid_argument(
        "canonical match teams must already exist in the catalog");
  }
  if (match.home_team_id == match.away_team_id) {
    throw std::invalid_argument("canonical match teams must be different");
  }
  if (match.home_score.has_value() != match.away_score.has_value() ||
      (match.home_score && (*match.home_score < 0 || *match.away_score < 0))) {
    throw std::invalid_argument(
        "canonical match scores must be non-negative and both present or missing");
  }
  if (!matches_.emplace(match.id, std::move(match)).second) {
    throw std::invalid_argument("duplicate canonical match ID");
  }
}

void CanonicalIdentityCatalog::mapTeam(ProviderTeamReference provider_team,
                                       CanonicalTeamId canonical_team) {
  requireProviderReference(provider_team.provider, provider_team.id);
  if (!teams_.contains(canonical_team)) {
    throw std::invalid_argument("cannot map an unknown canonical team ID");
  }
  addMapping(team_mappings_, std::move(provider_team), canonical_team);
}

void CanonicalIdentityCatalog::mapPlayer(ProviderPlayerReference provider_player,
                                         CanonicalPlayerId canonical_player) {
  requireProviderReference(provider_player.provider, provider_player.id);
  if (!players_.contains(canonical_player)) {
    throw std::invalid_argument("cannot map an unknown canonical player ID");
  }
  addMapping(player_mappings_, std::move(provider_player), canonical_player);
}

void CanonicalIdentityCatalog::mapMatch(ProviderMatchReference provider_match,
                                        CanonicalMatchId canonical_match) {
  requireProviderReference(provider_match.provider, provider_match.id);
  if (!matches_.contains(canonical_match)) {
    throw std::invalid_argument("cannot map an unknown canonical match ID");
  }
  addMapping(match_mappings_, std::move(provider_match), canonical_match);
}

void CanonicalIdentityCatalog::mapMetricaTeams(std::string provider_match_id,
                                               CanonicalTeamId home_team,
                                               CanonicalTeamId away_team) {
  if (provider_match_id.empty()) {
    throw std::invalid_argument("Metrica team mappings require a match ID");
  }
  mapTeam({"Metrica", "Home", provider_match_id}, home_team);
  mapTeam({"Metrica", "Away", std::move(provider_match_id)}, away_team);
}

const CanonicalTeam* CanonicalIdentityCatalog::team(CanonicalTeamId id) const {
  const auto position = teams_.find(id);
  return position == teams_.end() ? nullptr : &position->second;
}

const CanonicalPlayer* CanonicalIdentityCatalog::player(CanonicalPlayerId id) const {
  const auto position = players_.find(id);
  return position == players_.end() ? nullptr : &position->second;
}

const CanonicalMatch* CanonicalIdentityCatalog::match(CanonicalMatchId id) const {
  const auto position = matches_.find(id);
  return position == matches_.end() ? nullptr : &position->second;
}

std::optional<CanonicalTeamId> CanonicalIdentityCatalog::resolveTeam(
    const ProviderTeamReference& provider_team) const {
  return resolve(team_mappings_, provider_team);
}

std::optional<CanonicalPlayerId> CanonicalIdentityCatalog::resolvePlayer(
    const ProviderPlayerReference& provider_player) const {
  return resolve(player_mappings_, provider_player);
}

std::optional<CanonicalMatchId> CanonicalIdentityCatalog::resolveMatch(
    const ProviderMatchReference& provider_match) const {
  return resolve(match_mappings_, provider_match);
}

CanonicalEventIdentity CanonicalIdentityCatalog::resolveEvent(
    const FootballEvent& event) const {
  const auto provider_match_id = std::to_string(event.match_id);
  CanonicalEventIdentity result;
  result.match_id = resolveMatch({event.provider, provider_match_id});

  if (event.team_id) {
    result.team_id =
        resolveTeam({event.provider, std::to_string(*event.team_id), std::nullopt});
  } else if (event.team_name) {
    result.team_id = resolveTeam(
        {event.provider, *event.team_name, provider_match_id});
  }
  if (event.player_id) {
    result.player_id = resolvePlayer(
        {event.provider, std::to_string(*event.player_id), std::nullopt});
  } else if (event.player_name) {
    result.player_id = resolvePlayer(
        {event.provider, *event.player_name, provider_match_id});
  }
  return result;
}

const std::map<CanonicalTeamId, CanonicalTeam>&
CanonicalIdentityCatalog::teams() const noexcept {
  return teams_;
}

const std::map<CanonicalPlayerId, CanonicalPlayer>&
CanonicalIdentityCatalog::players() const noexcept {
  return players_;
}

const std::map<CanonicalMatchId, CanonicalMatch>&
CanonicalIdentityCatalog::matches() const noexcept {
  return matches_;
}

const std::map<ProviderTeamReference, CanonicalTeamId>&
CanonicalIdentityCatalog::teamMappings() const noexcept {
  return team_mappings_;
}

const std::map<ProviderPlayerReference, CanonicalPlayerId>&
CanonicalIdentityCatalog::playerMappings() const noexcept {
  return player_mappings_;
}

const std::map<ProviderMatchReference, CanonicalMatchId>&
CanonicalIdentityCatalog::matchMappings() const noexcept {
  return match_mappings_;
}

}  // namespace emberdb
