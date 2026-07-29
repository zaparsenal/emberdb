#include "emberdb/identity/canonical_identity.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace emberdb {
namespace {

template <typename Id>
void requirePositive(Id id, const char* name) {
  if (id.value <= 0) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
}

template <typename Entity>
void requireNewEntityState(const Entity& entity) {
  if (entity.status != CanonicalEntityStatus::Active ||
      entity.merged_into.has_value()) {
    throw std::invalid_argument(
        "new canonical entities must be active and not merged");
  }
}

void requireProviderReference(const std::string& provider, const std::string& id) {
  if (provider.empty() || id.empty()) {
    throw std::invalid_argument("provider mappings require non-empty provider and ID");
  }
}

template <typename Id, typename Entity>
Entity& requireEntity(std::map<Id, Entity>& entities, Id id,
                      std::string_view type) {
  const auto position = entities.find(id);
  if (position == entities.end()) {
    throw std::invalid_argument("unknown canonical " + std::string(type) +
                                " ID " + std::to_string(id.value));
  }
  return position->second;
}

template <typename Entity>
void requireActive(const Entity& entity, std::string_view type) {
  if (entity.status != CanonicalEntityStatus::Active) {
    throw std::invalid_argument("canonical " + std::string(type) + " ID " +
                                std::to_string(entity.id.value) +
                                " is not active");
  }
}

template <typename Id, typename Entity>
void renameEntity(std::map<Id, Entity>& entities, Id id, std::string name,
                  std::string_view type) {
  auto& entity = requireEntity(entities, id, type);
  requireActive(entity, type);
  if (name.empty()) {
    throw std::invalid_argument("canonical " + std::string(type) +
                                " name must not be empty");
  }
  entity.name = std::move(name);
}

template <typename Id, typename Entity>
void deprecateEntity(std::map<Id, Entity>& entities, Id id,
                     std::string_view type) {
  auto& entity = requireEntity(entities, id, type);
  if (entity.status == CanonicalEntityStatus::Merged) {
    throw std::invalid_argument("merged canonical " + std::string(type) +
                                " cannot be deprecated");
  }
  entity.status = CanonicalEntityStatus::Deprecated;
}

template <typename Id, typename Entity>
void prepareMerge(std::map<Id, Entity>& entities, Id source, Id target,
                  std::string_view type) {
  if (source == target) {
    throw std::invalid_argument("canonical " + std::string(type) +
                                " cannot be merged into itself");
  }
  requireActive(requireEntity(entities, source, type), type);
  requireActive(requireEntity(entities, target, type), type);
}

template <typename Id, typename Entity>
void recordMerge(std::map<Id, Entity>& entities, Id source, Id target) {
  for (auto& [unused, entity] : entities) {
    static_cast<void>(unused);
    if (entity.status == CanonicalEntityStatus::Merged &&
        entity.merged_into == source) {
      entity.merged_into = target;
    }
  }
  auto& source_entity = entities.at(source);
  source_entity.status = CanonicalEntityStatus::Merged;
  source_entity.merged_into = target;
}

template <typename Reference, typename Id>
void remap(std::map<Reference, Id>& mappings, Id source, Id target) {
  for (auto& [unused, canonical_id] : mappings) {
    static_cast<void>(unused);
    if (canonical_id == source) {
      canonical_id = target;
    }
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

void CanonicalIdentityCatalog::addCompetition(
    CanonicalCompetition competition) {
  requirePositive(competition.id, "canonical competition ID");
  requireNewEntityState(competition);
  if (competition.name.empty()) {
    throw std::invalid_argument(
        "canonical competition name must not be empty");
  }
  if (!competitions_.emplace(competition.id, std::move(competition)).second) {
    throw std::invalid_argument("duplicate canonical competition ID");
  }
}

void CanonicalIdentityCatalog::addSeason(CanonicalSeason season) {
  requirePositive(season.id, "canonical season ID");
  requireNewEntityState(season);
  const auto* competition = this->competition(season.competition_id);
  if (competition == nullptr) {
    throw std::invalid_argument(
        "canonical season competition must already exist in the catalog");
  }
  requireActive(*competition, "competition");
  if (season.name.empty()) {
    throw std::invalid_argument("canonical season name must not be empty");
  }
  if (!seasons_.emplace(season.id, std::move(season)).second) {
    throw std::invalid_argument("duplicate canonical season ID");
  }
}

void CanonicalIdentityCatalog::addTeam(CanonicalTeam team) {
  requirePositive(team.id, "canonical team ID");
  requireNewEntityState(team);
  if (team.name.empty()) {
    throw std::invalid_argument("canonical team name must not be empty");
  }
  if (!teams_.emplace(team.id, std::move(team)).second) {
    throw std::invalid_argument("duplicate canonical team ID");
  }
}

void CanonicalIdentityCatalog::addPlayer(CanonicalPlayer player) {
  requirePositive(player.id, "canonical player ID");
  requireNewEntityState(player);
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
  const auto* home_team = team(match.home_team_id);
  const auto* away_team = team(match.away_team_id);
  if (home_team == nullptr || away_team == nullptr) {
    throw std::invalid_argument(
        "canonical match teams must already exist in the catalog");
  }
  requireActive(*home_team, "team");
  requireActive(*away_team, "team");
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

void CanonicalIdentityCatalog::renameCompetition(
    CanonicalCompetitionId competition, std::string name) {
  const auto previous_name =
      requireEntity(competitions_, competition, "competition").name;
  renameEntity(competitions_, competition, std::move(name), "competition");
  const auto& current_name = competitions_.at(competition).name;
  for (auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if (match.competition == previous_name) {
      match.competition = current_name;
    }
  }
}

void CanonicalIdentityCatalog::renameSeason(CanonicalSeasonId season,
                                            std::string name) {
  const auto& entity = requireEntity(seasons_, season, "season");
  const auto previous_name = entity.name;
  const auto competition_name =
      requireEntity(competitions_, entity.competition_id, "competition").name;
  renameEntity(seasons_, season, std::move(name), "season");
  const auto& current_name = seasons_.at(season).name;
  for (auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if (match.competition == competition_name &&
        match.season == previous_name) {
      match.season = current_name;
    }
  }
}

void CanonicalIdentityCatalog::renameTeam(CanonicalTeamId team,
                                          std::string name) {
  renameEntity(teams_, team, std::move(name), "team");
}

void CanonicalIdentityCatalog::renamePlayer(CanonicalPlayerId player,
                                            std::string name) {
  renameEntity(players_, player, std::move(name), "player");
}

void CanonicalIdentityCatalog::deprecateCompetition(
    CanonicalCompetitionId competition) {
  for (const auto& [unused, season] : seasons_) {
    static_cast<void>(unused);
    if (season.competition_id == competition &&
        season.status == CanonicalEntityStatus::Active) {
      throw std::invalid_argument(
          "canonical competition cannot be deprecated while it has active "
          "seasons");
    }
  }
  deprecateEntity(competitions_, competition, "competition");
}

void CanonicalIdentityCatalog::deprecateSeason(CanonicalSeasonId season) {
  deprecateEntity(seasons_, season, "season");
}

void CanonicalIdentityCatalog::deprecateTeam(CanonicalTeamId team) {
  deprecateEntity(teams_, team, "team");
}

void CanonicalIdentityCatalog::deprecatePlayer(CanonicalPlayerId player) {
  deprecateEntity(players_, player, "player");
}

void CanonicalIdentityCatalog::mergeCompetition(
    CanonicalCompetitionId source, CanonicalCompetitionId target) {
  prepareMerge(competitions_, source, target, "competition");
  const auto source_name = competitions_.at(source).name;
  const auto target_name = competitions_.at(target).name;
  for (auto& [unused, season] : seasons_) {
    static_cast<void>(unused);
    if (season.competition_id == source) {
      season.competition_id = target;
    }
  }
  for (auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if (match.competition == source_name) {
      match.competition = target_name;
    }
  }
  remap(competition_mappings_, source, target);
  recordMerge(competitions_, source, target);
}

void CanonicalIdentityCatalog::mergeSeason(CanonicalSeasonId source,
                                           CanonicalSeasonId target) {
  prepareMerge(seasons_, source, target, "season");
  const auto& source_entity = seasons_.at(source);
  const auto& target_entity = seasons_.at(target);
  if (source_entity.competition_id != target_entity.competition_id) {
    throw std::invalid_argument(
        "canonical seasons must belong to the same competition to merge");
  }
  const auto competition_name =
      competitions_.at(target_entity.competition_id).name;
  for (auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if (match.competition == competition_name &&
        match.season == source_entity.name) {
      match.season = target_entity.name;
    }
  }
  remap(season_mappings_, source, target);
  recordMerge(seasons_, source, target);
}

void CanonicalIdentityCatalog::mergeTeam(CanonicalTeamId source,
                                         CanonicalTeamId target) {
  prepareMerge(teams_, source, target, "team");
  for (const auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if ((match.home_team_id == source && match.away_team_id == target) ||
        (match.home_team_id == target && match.away_team_id == source)) {
      throw std::invalid_argument(
          "canonical team merge would collapse both sides of a match");
    }
  }
  for (auto& [unused, match] : matches_) {
    static_cast<void>(unused);
    if (match.home_team_id == source) {
      match.home_team_id = target;
    }
    if (match.away_team_id == source) {
      match.away_team_id = target;
    }
  }
  remap(team_mappings_, source, target);
  recordMerge(teams_, source, target);
}

void CanonicalIdentityCatalog::mergePlayer(CanonicalPlayerId source,
                                           CanonicalPlayerId target) {
  prepareMerge(players_, source, target, "player");
  remap(player_mappings_, source, target);
  recordMerge(players_, source, target);
}

void CanonicalIdentityCatalog::mapCompetition(
    ProviderCompetitionReference provider_competition,
    CanonicalCompetitionId canonical_competition) {
  requireProviderReference(provider_competition.provider,
                           provider_competition.id);
  const auto* competition = this->competition(canonical_competition);
  if (competition == nullptr) {
    throw std::invalid_argument(
        "cannot map an unknown canonical competition ID");
  }
  requireActive(*competition, "competition");
  addMapping(competition_mappings_, std::move(provider_competition),
             canonical_competition);
}

void CanonicalIdentityCatalog::mapSeason(
    ProviderSeasonReference provider_season,
    CanonicalSeasonId canonical_season) {
  requireProviderReference(provider_season.provider, provider_season.id);
  const auto* season = this->season(canonical_season);
  if (season == nullptr) {
    throw std::invalid_argument("cannot map an unknown canonical season ID");
  }
  requireActive(*season, "season");
  const auto* competition = this->competition(season->competition_id);
  if (competition == nullptr) {
    throw std::logic_error(
        "canonical season references an unknown competition");
  }
  requireActive(*competition, "competition");
  addMapping(season_mappings_, std::move(provider_season), canonical_season);
}

void CanonicalIdentityCatalog::mapTeam(ProviderTeamReference provider_team,
                                       CanonicalTeamId canonical_team) {
  requireProviderReference(provider_team.provider, provider_team.id);
  const auto* team = this->team(canonical_team);
  if (team == nullptr) {
    throw std::invalid_argument("cannot map an unknown canonical team ID");
  }
  requireActive(*team, "team");
  addMapping(team_mappings_, std::move(provider_team), canonical_team);
}

void CanonicalIdentityCatalog::mapPlayer(ProviderPlayerReference provider_player,
                                         CanonicalPlayerId canonical_player) {
  requireProviderReference(provider_player.provider, provider_player.id);
  const auto* player = this->player(canonical_player);
  if (player == nullptr) {
    throw std::invalid_argument("cannot map an unknown canonical player ID");
  }
  requireActive(*player, "player");
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

const CanonicalCompetition* CanonicalIdentityCatalog::competition(
    CanonicalCompetitionId id) const {
  const auto position = competitions_.find(id);
  return position == competitions_.end() ? nullptr : &position->second;
}

const CanonicalSeason* CanonicalIdentityCatalog::season(
    CanonicalSeasonId id) const {
  const auto position = seasons_.find(id);
  return position == seasons_.end() ? nullptr : &position->second;
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

std::optional<CanonicalCompetitionId>
CanonicalIdentityCatalog::resolveCompetition(
    const ProviderCompetitionReference& provider_competition) const {
  return resolve(competition_mappings_, provider_competition);
}

std::optional<CanonicalSeasonId> CanonicalIdentityCatalog::resolveSeason(
    const ProviderSeasonReference& provider_season) const {
  return resolve(season_mappings_, provider_season);
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

const std::map<CanonicalCompetitionId, CanonicalCompetition>&
CanonicalIdentityCatalog::competitions() const noexcept {
  return competitions_;
}

const std::map<CanonicalSeasonId, CanonicalSeason>&
CanonicalIdentityCatalog::seasons() const noexcept {
  return seasons_;
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

const std::map<ProviderCompetitionReference, CanonicalCompetitionId>&
CanonicalIdentityCatalog::competitionMappings() const noexcept {
  return competition_mappings_;
}

const std::map<ProviderSeasonReference, CanonicalSeasonId>&
CanonicalIdentityCatalog::seasonMappings() const noexcept {
  return season_mappings_;
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

std::string_view canonicalEntityStatusName(
    CanonicalEntityStatus status) noexcept {
  switch (status) {
    case CanonicalEntityStatus::Active:
      return "active";
    case CanonicalEntityStatus::Deprecated:
      return "deprecated";
    case CanonicalEntityStatus::Merged:
      return "merged";
  }
  return "unknown";
}

}  // namespace emberdb
