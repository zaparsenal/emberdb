#include "emberdb/ingestion/statsbomb_metadata_adapter.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "metadata_json.h"

namespace emberdb {
namespace {

using metadata_json::Json;
constexpr std::string_view kProvider = "StatsBomb";

template <typename T>
T required(const Json& object, std::string_view key,
           const std::filesystem::path& path, std::size_t index) {
  return metadata_json::required<T>(object, key, kProvider, path, index);
}

const Json& requiredObject(const Json& object, std::string_view key,
                           const std::filesystem::path& path, std::size_t index) {
  const auto value = object.find(key);
  if (value == object.end() || value->is_null()) {
    throw metadata_json::recordError(
        kProvider, path, index,
        "missing required field '" + std::string(key) + "'");
  }
  if (!value->is_object()) {
    throw metadata_json::recordError(
        kProvider, path, index,
        "field '" + std::string(key) + "' must be an object");
  }
  return *value;
}

ProviderTeamMetadata team(const Json& object, std::string_view id_key,
                          std::string_view name_key,
                          const std::filesystem::path& path, std::size_t index) {
  const auto id = required<Identifier>(object, id_key, path, index);
  const auto name = required<std::string>(object, name_key, path, index);
  if (name.empty()) {
    throw metadata_json::recordError(kProvider, path, index,
                                     "team name must not be empty");
  }
  return {{std::string(kProvider), std::to_string(id), std::nullopt}, name};
}

}  // namespace

ProviderMetadata StatsBombMetadataAdapter::loadMatches(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  metadata.matches.reserve(document.size());
  metadata.teams.reserve(document.size() * 2);
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    const auto& competition = requiredObject(record, "competition", path, index);
    const auto& season = requiredObject(record, "season", path, index);
    auto home = team(requiredObject(record, "home_team", path, index),
                     "home_team_id", "home_team_name", path, index);
    auto away = team(requiredObject(record, "away_team", path, index),
                     "away_team_id", "away_team_name", path, index);
    const auto match_id = required<Identifier>(record, "match_id", path, index);
    const auto competition_id =
        required<Identifier>(competition, "competition_id", path, index);
    const auto season_id = required<Identifier>(season, "season_id", path, index);
    const auto date = required<std::string>(record, "match_date", path, index);
    const auto kickoff = metadata_json::optional<std::string>(
        record, "kick_off", kProvider, path, index);
    const auto home_score = metadata_json::optional<std::int32_t>(
        record, "home_score", kProvider, path, index);
    const auto away_score = metadata_json::optional<std::int32_t>(
        record, "away_score", kProvider, path, index);
    if ((home_score && *home_score < 0) || (away_score && *away_score < 0)) {
      throw metadata_json::recordError(kProvider, path, index,
                                       "scores must be non-negative");
    }
    metadata.matches.push_back(
        {{std::string(kProvider), std::to_string(match_id)},
         std::to_string(competition_id),
         required<std::string>(competition, "competition_name", path, index),
         std::to_string(season_id),
         required<std::string>(season, "season_name", path, index),
         kickoff ? std::optional<std::chrono::sys_seconds>{
                       metadata_json::parseDateAndTime(date, *kickoff, kProvider, path,
                                                       index)}
                 : std::nullopt,
         home.reference,
         away.reference,
         home_score,
         away_score});
    metadata.teams.push_back(std::move(home));
    metadata.teams.push_back(std::move(away));
  }
  return metadata;
}

ProviderMetadata StatsBombMetadataAdapter::loadLineups(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    auto provider_team = team(record, "team_id", "team_name", path, index);
    const auto lineup = record.find("lineup");
    if (lineup == record.end() || !lineup->is_array()) {
      throw metadata_json::recordError(kProvider, path, index,
                                       "field 'lineup' must be an array");
    }
    for (std::size_t player_index = 0; player_index < lineup->size(); ++player_index) {
      const auto& player = (*lineup)[player_index];
      if (!player.is_object()) {
        throw metadata_json::recordError(
            kProvider, path, index,
            "lineup entry at index " + std::to_string(player_index) +
                " must be an object");
      }
      const auto player_id = required<Identifier>(player, "player_id", path, index);
      const auto player_name =
          required<std::string>(player, "player_name", path, index);
      if (player_name.empty()) {
        throw metadata_json::recordError(kProvider, path, index,
                                         "player_name must not be empty");
      }
      metadata.players.push_back(
          {{std::string(kProvider), std::to_string(player_id), std::nullopt},
           player_name, provider_team.reference});
    }
    metadata.teams.push_back(std::move(provider_team));
  }
  return metadata;
}

}  // namespace emberdb
