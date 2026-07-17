#include "emberdb/ingestion/wyscout_metadata_adapter.h"

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "metadata_json.h"

namespace emberdb {
namespace {

using metadata_json::Json;
constexpr std::string_view kProvider = "Wyscout";

template <typename T>
T required(const Json& object, std::string_view key,
           const std::filesystem::path& path, std::size_t index) {
  return metadata_json::required<T>(object, key, kProvider, path, index);
}

std::string playerName(const Json& player, const std::filesystem::path& path,
                       std::size_t index) {
  const auto short_name = player.find("shortName");
  if (short_name != player.end() && short_name->is_string() &&
      !short_name->get_ref<const std::string&>().empty()) {
    return short_name->get<std::string>();
  }
  const auto first = required<std::string>(player, "firstName", path, index);
  const auto last = required<std::string>(player, "lastName", path, index);
  const auto name = first.empty() ? last : (last.empty() ? first : first + " " + last);
  if (name.empty()) {
    throw metadata_json::recordError(kProvider, path, index,
                                     "player name must not be empty");
  }
  return name;
}

}  // namespace

ProviderMetadata WyscoutMetadataAdapter::loadCompetitions(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  metadata.competitions.reserve(document.size());
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    const auto id = required<Identifier>(record, "wyId", path, index);
    const auto name = required<std::string>(record, "name", path, index);
    if (name.empty()) {
      throw metadata_json::recordError(kProvider, path, index,
                                       "competition name must not be empty");
    }
    metadata.competitions.push_back(
        {std::string(kProvider), std::to_string(id), name});
  }
  return metadata;
}

ProviderMetadata WyscoutMetadataAdapter::loadTeams(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  metadata.teams.reserve(document.size());
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    const auto id = required<Identifier>(record, "wyId", path, index);
    const auto name = required<std::string>(record, "name", path, index);
    if (name.empty()) {
      throw metadata_json::recordError(kProvider, path, index,
                                       "team name must not be empty");
    }
    metadata.teams.push_back(
        {{std::string(kProvider), std::to_string(id), std::nullopt}, name});
  }
  return metadata;
}

ProviderMetadata WyscoutMetadataAdapter::loadPlayers(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  metadata.players.reserve(document.size());
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    const auto id = required<Identifier>(record, "wyId", path, index);
    std::optional<ProviderTeamReference> current_team;
    const auto current_team_value = record.find("currentTeamId");
    if (current_team_value != record.end() && !current_team_value->is_null()) {
      if (current_team_value->is_string() &&
          current_team_value->get_ref<const std::string&>() == "null") {
        // The open research export uses both JSON null and the string "null".
      } else if (current_team_value->is_number_integer()) {
        const auto current_team_id = current_team_value->get<Identifier>();
        if (current_team_id != 0) {
          current_team = ProviderTeamReference{
              std::string(kProvider), std::to_string(current_team_id), std::nullopt};
        }
      } else {
        throw metadata_json::recordError(
            kProvider, path, index,
            "field 'currentTeamId' must be an integer or null");
      }
    }
    metadata.players.push_back(
        {{std::string(kProvider), std::to_string(id), std::nullopt},
         playerName(record, path, index), std::move(current_team)});
  }
  return metadata;
}

ProviderMetadata WyscoutMetadataAdapter::loadMatches(
    const std::filesystem::path& path) const {
  const auto document = metadata_json::readArray(kProvider, path);
  ProviderMetadata metadata;
  metadata.matches.reserve(document.size());
  for (std::size_t index = 0; index < document.size(); ++index) {
    const auto& record = document[index];
    if (!record.is_object()) {
      throw metadata_json::recordError(kProvider, path, index, "expected an object");
    }
    const auto match_id = required<Identifier>(record, "wyId", path, index);
    const auto competition_id =
        required<Identifier>(record, "competitionId", path, index);
    const auto season_id = required<Identifier>(record, "seasonId", path, index);
    const auto date_utc = required<std::string>(record, "dateutc", path, index);
    if (date_utc.size() != 19 || date_utc[10] != ' ') {
      throw metadata_json::recordError(kProvider, path, index,
                                       "dateutc must use YYYY-MM-DD hh:mm:ss");
    }
    const auto teams_data = record.find("teamsData");
    if (teams_data == record.end() || !teams_data->is_object()) {
      throw metadata_json::recordError(kProvider, path, index,
                                       "field 'teamsData' must be an object");
    }
    std::optional<ProviderTeamReference> home_team;
    std::optional<ProviderTeamReference> away_team;
    std::optional<std::int32_t> home_score;
    std::optional<std::int32_t> away_score;
    for (const auto& [key, value] : teams_data->items()) {
      if (!value.is_object()) {
        throw metadata_json::recordError(kProvider, path, index,
                                         "teamsData entries must be objects");
      }
      const auto team_id = required<Identifier>(value, "teamId", path, index);
      if (key != std::to_string(team_id)) {
        throw metadata_json::recordError(kProvider, path, index,
                                         "teamsData key does not match teamId");
      }
      const auto side = required<std::string>(value, "side", path, index);
      const auto score = metadata_json::optional<std::int32_t>(
          value, "score", kProvider, path, index);
      if (score && *score < 0) {
        throw metadata_json::recordError(kProvider, path, index,
                                         "score must be non-negative");
      }
      ProviderTeamReference reference{std::string(kProvider), key, std::nullopt};
      if (side == "home" && !home_team) {
        home_team = std::move(reference);
        home_score = score;
      } else if (side == "away" && !away_team) {
        away_team = std::move(reference);
        away_score = score;
      } else {
        throw metadata_json::recordError(
            kProvider, path, index,
            "teamsData must contain one home team and one away team");
      }
    }
    if (!home_team || !away_team) {
      throw metadata_json::recordError(
          kProvider, path, index,
          "teamsData must contain one home team and one away team");
    }
    metadata.matches.push_back(
        {{std::string(kProvider), std::to_string(match_id)},
         std::to_string(competition_id),
         std::nullopt,
         std::to_string(season_id),
         std::nullopt,
         metadata_json::parseDateAndTime(date_utc.substr(0, 10),
                                         date_utc.substr(11), kProvider, path, index),
         *home_team,
         *away_team,
         home_score,
         away_score});
  }
  return metadata;
}

}  // namespace emberdb
