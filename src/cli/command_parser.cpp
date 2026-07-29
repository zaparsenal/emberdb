#include "cli/command_parser.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "emberdb/common/football_event_column.h"

namespace emberdb::cli {
namespace {

template <typename Integer>
Integer parseInteger(std::string_view text, std::string_view option) {
  Integer value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error("Invalid value for " + std::string(option) + ": '" +
                             std::string(text) + "'");
  }
  return value;
}

FootballEventColumn parseColumn(std::string_view name) {
  const auto column = columnFromName(name);
  if (!column) {
    throw std::runtime_error("Unknown column '" + std::string(name) + "'");
  }
  return *column;
}

std::vector<FootballEventColumn> parseProjection(std::string_view text) {
  std::vector<FootballEventColumn> columns;
  while (!text.empty()) {
    const auto separator = text.find(',');
    const auto name = text.substr(0, separator);
    if (name.empty()) {
      throw std::runtime_error("Projection contains an empty column name");
    }
    columns.push_back(parseColumn(name));
    if (separator == std::string_view::npos) {
      break;
    }
    text.remove_prefix(separator + 1);
    if (text.empty()) {
      throw std::runtime_error("Projection contains an empty column name");
    }
  }
  return columns;
}

FootballEventValue parseFilterValue(FootballEventColumn column,
                                    std::string_view text) {
  switch (columnValueType(column)) {
    case FootballEventValueType::Identifier:
      return parseInteger<Identifier>(text, "filter");
    case FootballEventValueType::Integer:
      return parseInteger<std::int32_t>(text, "filter");
    case FootballEventValueType::Timestamp:
      return std::chrono::milliseconds{
          parseInteger<std::int64_t>(text, "filter")};
    case FootballEventValueType::Number: {
      double value{};
      std::istringstream input{std::string(text)};
      input >> std::noskipws >> value;
      if (!input || !input.eof()) {
        throw std::runtime_error("Invalid numeric filter value '" +
                                 std::string(text) + "'");
      }
      return value;
    }
    case FootballEventValueType::Text:
      return std::string(text);
  }
  throw std::runtime_error("Unsupported filter value type");
}

EqualityPredicate parseFilter(std::string_view text) {
  const auto separator = text.find('=');
  if (separator == std::string_view::npos || separator == 0) {
    throw std::runtime_error("Filter must have the form COLUMN=VALUE");
  }
  const auto column = parseColumn(text.substr(0, separator));
  return {column, parseFilterValue(column, text.substr(separator + 1))};
}

AggregateFunction parseAggregateFunction(std::string_view name) {
  if (name == "count") {
    return AggregateFunction::Count;
  }
  if (name == "sum") {
    return AggregateFunction::Sum;
  }
  if (name == "avg") {
    return AggregateFunction::Average;
  }
  if (name == "min") {
    return AggregateFunction::Minimum;
  }
  if (name == "max") {
    return AggregateFunction::Maximum;
  }
  throw std::runtime_error("Unknown aggregate function '" + std::string(name) +
                           "'");
}

AggregateExpression parseAggregate(std::string_view text) {
  const auto open = text.find('(');
  if (open == std::string_view::npos || open == 0 || text.back() != ')' ||
      text.find(')', open) != text.size() - 1) {
    throw std::runtime_error(
        "Aggregate must have the form FUNCTION(COLUMN) or count(*)");
  }
  const auto function = parseAggregateFunction(text.substr(0, open));
  const auto input = text.substr(open + 1, text.size() - open - 2);
  if (input == "*") {
    if (function != AggregateFunction::Count) {
      throw std::runtime_error("Only count(*) accepts '*'");
    }
    return {function};
  }
  if (input.empty()) {
    throw std::runtime_error("Aggregate input column cannot be empty");
  }
  return {function, parseColumn(input)};
}

CatalogEntityType parseCatalogEntity(std::string_view value) {
  if (value == "competition") return CatalogEntityType::Competition;
  if (value == "season") return CatalogEntityType::Season;
  if (value == "team") return CatalogEntityType::Team;
  if (value == "player") return CatalogEntityType::Player;
  if (value == "match") return CatalogEntityType::Match;
  throw std::runtime_error(
      "--entity must be competition, season, team, player, or match");
}

}  // namespace

Options parseOptions(std::span<const std::string_view> arguments) {
  if (arguments.size() < 2) {
    throw std::runtime_error(
        "Expected the 'import', 'query', 'reconcile', or 'catalog' command");
  }
  Options options;
  const auto command = arguments[1];
  std::size_t first_option = 2;
  if (command == "import") {
    options.command = Command::Import;
  } else if (command == "query") {
    options.command = Command::Query;
  } else if (command == "reconcile") {
    if (arguments.size() < 3) {
      throw std::runtime_error("Expected a reconcile action");
    }
    const auto action = arguments[2];
    if (action == "generate") {
      options.command = Command::ReconcileGenerate;
    } else if (action == "list") {
      options.command = Command::ReconcileList;
    } else if (action == "inspect") {
      options.command = Command::ReconcileInspect;
    } else if (action == "accept") {
      options.command = Command::ReconcileAccept;
    } else if (action == "reject") {
      options.command = Command::ReconcileReject;
    } else {
      throw std::runtime_error("Unknown reconcile action '" +
                               std::string(action) + "'");
    }
    first_option = 3;
  } else if (command == "catalog") {
    if (arguments.size() < 3) {
      throw std::runtime_error("Expected a catalog action");
    }
    const auto action = arguments[2];
    if (action == "init") {
      options.command = Command::CatalogInit;
    } else if (action == "import") {
      options.command = Command::CatalogImport;
    } else if (action == "add") {
      options.command = Command::CatalogAdd;
    } else if (action == "map") {
      options.command = Command::CatalogMap;
    } else if (action == "rename") {
      options.command = Command::CatalogRename;
    } else if (action == "deprecate") {
      options.command = Command::CatalogDeprecate;
    } else if (action == "merge") {
      options.command = Command::CatalogMerge;
    } else if (action == "list") {
      options.command = Command::CatalogList;
    } else if (action == "history") {
      options.command = Command::CatalogHistory;
    } else if (action == "validate") {
      options.command = Command::CatalogValidate;
    } else if (action == "candidates") {
      if (arguments.size() < 4) {
        throw std::runtime_error("Expected a catalog candidates action");
      }
      const auto candidate_action = arguments[3];
      if (candidate_action == "generate") {
        options.command = Command::EntityCandidateGenerate;
      } else if (candidate_action == "list") {
        options.command = Command::EntityCandidateList;
      } else if (candidate_action == "inspect") {
        options.command = Command::EntityCandidateInspect;
      } else if (candidate_action == "accept") {
        options.command = Command::EntityCandidateAccept;
      } else if (candidate_action == "reject") {
        options.command = Command::EntityCandidateReject;
      } else {
        throw std::runtime_error(
            "Unknown catalog candidates action '" +
            std::string(candidate_action) + "'");
      }
      first_option = 4;
    } else {
      throw std::runtime_error("Unknown catalog action '" +
                               std::string(action) + "'");
    }
    if (action != "candidates") {
      first_option = 3;
    }
  } else {
    throw std::runtime_error(
        "Expected the 'import', 'query', 'reconcile', or 'catalog' command");
  }

  for (std::size_t index = first_option; index < arguments.size(); ++index) {
    const auto option = arguments[index];
    if (option == "--dry-run") {
      if (options.dry_run) {
        throw std::runtime_error("--dry-run may only be specified once");
      }
      options.dry_run = true;
      continue;
    }
    if (index + 1 >= arguments.size()) {
      throw std::runtime_error("Missing value for " + std::string(option));
    }
    const auto value = arguments[++index];
    if (option == "--provider") {
      options.provider = value;
    } else if (option == "--match-id") {
      options.match_id = parseInteger<Identifier>(value, option);
      options.has_match_id = true;
    } else if (option == "--input") {
      options.input = value;
    } else if (option == "--output") {
      options.output = value;
    } else if (option == "--database") {
      options.database = value;
    } else if (option == "--limit") {
      options.limit = parseInteger<std::size_t>(value, option);
      options.has_limit = true;
    } else if (option == "--filter") {
      options.filters.emplace_back(value);
    } else if (option == "--project") {
      if (!options.projection.empty()) {
        throw std::runtime_error("--project may only be specified once");
      }
      options.projection = value;
    } else if (option == "--group-by") {
      if (!options.group_by.empty()) {
        throw std::runtime_error("--group-by may only be specified once");
      }
      options.group_by = value;
    } else if (option == "--aggregate") {
      options.aggregates.emplace_back(value);
    } else if (option == "--home-first-half-direction") {
      if (options.home_first_half_direction) {
        throw std::runtime_error(
            "--home-first-half-direction may only be specified once");
      }
      if (value == "left-to-right") {
        options.home_first_half_direction = AttackingDirection::LeftToRight;
      } else if (value == "right-to-left") {
        options.home_first_half_direction = AttackingDirection::RightToLeft;
      } else {
        throw std::runtime_error(
            "--home-first-half-direction must be left-to-right or right-to-left");
      }
    } else if (option == "--review") {
      options.review = value;
    } else if (option == "--manifest") {
      options.manifest = value;
    } else if (option == "--store") {
      options.store = value;
    } else if (option == "--left-provider") {
      options.left_provider = value;
    } else if (option == "--left-input") {
      options.left_input = value;
    } else if (option == "--right-provider") {
      options.right_provider = value;
    } else if (option == "--right-input") {
      options.right_input = value;
    } else if (option == "--candidate-id") {
      options.candidate_id = parseInteger<std::uint64_t>(value, option);
      options.has_candidate_id = true;
    } else if (option == "--canonical-match-id") {
      options.canonical_match_id = parseInteger<Identifier>(value, option);
      options.has_canonical_match_id = true;
    } else if (option == "--status") {
      if (value == "unresolved") {
        options.candidate_status = MatchCandidateStatus::Unresolved;
      } else if (value == "accepted") {
        options.candidate_status = MatchCandidateStatus::Accepted;
      } else if (value == "rejected") {
        options.candidate_status = MatchCandidateStatus::Rejected;
      } else {
        throw std::runtime_error(
            "--status must be unresolved, accepted, or rejected");
      }
    } else if (option == "--entity") {
      if (options.catalog_entity) {
        throw std::runtime_error("--entity may only be specified once");
      }
      options.catalog_entity = parseCatalogEntity(value);
    } else if (option == "--canonical-id") {
      options.canonical_id = parseInteger<Identifier>(value, option);
      options.has_canonical_id = true;
    } else if (option == "--target-canonical-id") {
      options.target_canonical_id = parseInteger<Identifier>(value, option);
      options.has_target_canonical_id = true;
    } else if (option == "--name") {
      options.name = value;
    } else if (option == "--competition-id") {
      options.competition_id = parseInteger<Identifier>(value, option);
      options.has_competition_id = true;
    } else if (option == "--competition") {
      options.competition = value;
    } else if (option == "--season") {
      options.season = value;
    } else if (option == "--home-team-id") {
      options.home_team_id = parseInteger<Identifier>(value, option);
      options.has_home_team_id = true;
    } else if (option == "--away-team-id") {
      options.away_team_id = parseInteger<Identifier>(value, option);
      options.has_away_team_id = true;
    } else if (option == "--kickoff-seconds") {
      options.kickoff_seconds = parseInteger<std::int64_t>(value, option);
    } else if (option == "--home-score") {
      options.home_score = parseInteger<std::int32_t>(value, option);
    } else if (option == "--away-score") {
      options.away_score = parseInteger<std::int32_t>(value, option);
    } else if (option == "--provider-id") {
      options.provider_id = value;
    } else if (option == "--provider-match-id") {
      options.provider_match_id = value;
    } else if (option == "--actor") {
      options.actor = value;
    } else if (option == "--source") {
      options.source = value;
    } else if (option == "--reason") {
      options.reason = value;
    } else {
      throw std::runtime_error("Unknown option '" + std::string(option) + "'");
    }
  }

  const bool is_entity_review =
      options.command == Command::EntityCandidateGenerate ||
      options.command == Command::EntityCandidateList ||
      options.command == Command::EntityCandidateInspect ||
      options.command == Command::EntityCandidateAccept ||
      options.command == Command::EntityCandidateReject;
  const bool is_catalog_validation =
      options.command == Command::CatalogValidate;
  const bool is_catalog_import =
      options.command == Command::CatalogImport;
  const bool is_catalog =
      options.command == Command::CatalogInit ||
      is_catalog_import ||
      options.command == Command::CatalogAdd ||
      options.command == Command::CatalogMap ||
      options.command == Command::CatalogRename ||
      options.command == Command::CatalogDeprecate ||
      options.command == Command::CatalogMerge ||
      options.command == Command::CatalogList ||
      options.command == Command::CatalogHistory ||
      is_catalog_validation || is_entity_review;
  const bool has_catalog_identity_options =
      options.catalog_entity || options.has_canonical_id ||
      options.has_target_canonical_id ||
      !options.name.empty() || options.has_competition_id ||
      !options.competition.empty() || !options.season.empty() ||
      options.has_home_team_id || options.has_away_team_id ||
      options.kickoff_seconds || options.home_score || options.away_score ||
      !options.provider_id.empty() || options.provider_match_id;
  if (is_catalog) {
    if (is_catalog_import) {
      const bool has_unrelated_options =
          !options.review.empty() || !options.provider.empty() ||
          options.has_match_id || !options.input.empty() ||
          !options.output.empty() || !options.database.empty() ||
          options.has_limit || !options.filters.empty() ||
          !options.projection.empty() || !options.group_by.empty() ||
          !options.aggregates.empty() ||
          options.home_first_half_direction.has_value() ||
          !options.left_provider.empty() || !options.left_input.empty() ||
          !options.right_provider.empty() || !options.right_input.empty() ||
          options.has_candidate_id || options.has_canonical_match_id ||
          options.candidate_status || has_catalog_identity_options ||
          !options.actor.empty() || !options.source.empty() ||
          !options.reason.empty();
      if (has_unrelated_options) {
        throw std::runtime_error(
            "catalog import accepts only --manifest, --store, and --dry-run");
      }
      if (options.manifest.empty() || options.store.empty()) {
        throw std::runtime_error(
            "catalog import requires --manifest and --store");
      }
      return options;
    }
    if (!options.manifest.empty() || !options.store.empty() ||
        options.dry_run) {
      throw std::runtime_error(
          "--manifest, --store, and --dry-run are only valid for catalog "
          "import");
    }
    if (options.review.empty()) {
      throw std::runtime_error("--review is required for catalog commands");
    }
    if (is_catalog_validation) {
      const bool has_unrelated_options =
          options.has_match_id || !options.output.empty() ||
          !options.database.empty() || options.has_limit ||
          !options.filters.empty() || !options.projection.empty() ||
          !options.group_by.empty() || !options.aggregates.empty() ||
          options.home_first_half_direction.has_value() ||
          !options.left_provider.empty() || !options.left_input.empty() ||
          !options.right_provider.empty() || !options.right_input.empty() ||
          options.has_candidate_id || options.has_canonical_match_id ||
          options.candidate_status || options.has_canonical_id ||
          options.has_target_canonical_id || !options.name.empty() ||
          options.has_competition_id || !options.competition.empty() ||
          !options.season.empty() || options.has_home_team_id ||
          options.has_away_team_id || options.kickoff_seconds ||
          options.home_score || options.away_score ||
          !options.provider_id.empty() || options.provider_match_id ||
          !options.actor.empty() || !options.source.empty() ||
          !options.reason.empty();
      if (has_unrelated_options) {
        throw std::runtime_error(
            "catalog validate accepts only --review, --entity, --provider, "
            "and --input");
      }
      if (!options.catalog_entity ||
          *options.catalog_entity == CatalogEntityType::Match ||
          options.provider.empty() || options.input.empty()) {
        throw std::runtime_error(
            "catalog validate requires --entity "
            "competition|season|team|player, --provider, and --input");
      }
      return options;
    }
    if (is_entity_review) {
      const bool has_unrelated_options =
          options.has_match_id || !options.output.empty() ||
          !options.database.empty() || options.has_limit ||
          !options.filters.empty() || !options.projection.empty() ||
          !options.group_by.empty() || !options.aggregates.empty() ||
          options.home_first_half_direction.has_value() ||
          !options.left_provider.empty() || !options.left_input.empty() ||
          !options.right_provider.empty() || !options.right_input.empty() ||
          options.has_canonical_match_id || options.has_canonical_id ||
          options.has_target_canonical_id ||
          !options.name.empty() || options.has_competition_id ||
          !options.competition.empty() || !options.season.empty() ||
          options.has_home_team_id || options.has_away_team_id ||
          options.kickoff_seconds || options.home_score ||
          options.away_score || !options.provider_id.empty() ||
          options.provider_match_id;
      if (has_unrelated_options) {
        throw std::runtime_error(
            "unrelated event, reconciliation, or authoring options are not valid "
            "for catalog candidate commands");
      }
      if (options.catalog_entity &&
          *options.catalog_entity == CatalogEntityType::Match) {
        throw std::runtime_error(
            "catalog entity candidates support competition, season, team, or player");
      }
      if (options.command == Command::EntityCandidateGenerate) {
        if (!options.catalog_entity || options.provider.empty() ||
            options.input.empty()) {
          throw std::runtime_error(
              "catalog candidates generate requires --entity, --provider, and --input");
        }
        if (options.has_candidate_id || options.candidate_status ||
            !options.actor.empty() || !options.source.empty() ||
            !options.reason.empty()) {
          throw std::runtime_error(
              "decision and status options are not valid for candidate generation");
        }
        return options;
      }
      if (options.command == Command::EntityCandidateList) {
        if (!options.provider.empty() || !options.input.empty() ||
            options.has_candidate_id || !options.actor.empty() ||
            !options.source.empty() || !options.reason.empty()) {
          throw std::runtime_error(
              "catalog candidates list accepts only --review, --entity, and --status");
        }
        return options;
      }
      const bool is_decision =
          options.command == Command::EntityCandidateAccept ||
          options.command == Command::EntityCandidateReject;
      if (!options.has_candidate_id || options.candidate_id == 0) {
        throw std::runtime_error("a positive --candidate-id is required");
      }
      if (!options.provider.empty() || !options.input.empty() ||
          options.catalog_entity || options.candidate_status) {
        throw std::runtime_error(
            "provider, entity, input, and status options are not valid for this "
            "catalog candidate action");
      }
      if (is_decision &&
          (options.actor.empty() || options.source.empty() ||
           options.reason.empty())) {
        throw std::runtime_error(
            "--actor, --source, and --reason are required for catalog candidate "
            "decisions");
      }
      if (!is_decision &&
          (!options.actor.empty() || !options.source.empty() ||
           !options.reason.empty())) {
        throw std::runtime_error(
            "review provenance is only valid for catalog candidate decisions");
      }
      return options;
    }
    const bool has_non_catalog_options =
        options.has_match_id || !options.input.empty() ||
        !options.output.empty() || !options.database.empty() ||
        options.has_limit || !options.filters.empty() ||
        !options.projection.empty() || !options.group_by.empty() ||
        !options.aggregates.empty() ||
        options.home_first_half_direction.has_value() ||
        !options.left_provider.empty() || !options.left_input.empty() ||
        !options.right_provider.empty() || !options.right_input.empty() ||
        options.has_candidate_id || options.has_canonical_match_id ||
        options.candidate_status;
    if (has_non_catalog_options) {
      throw std::runtime_error(
          "event, query, and reconciliation options are not valid for catalog commands");
    }
    const bool is_mutation =
        options.command == Command::CatalogAdd ||
        options.command == Command::CatalogMap ||
        options.command == Command::CatalogRename ||
        options.command == Command::CatalogDeprecate ||
        options.command == Command::CatalogMerge;
    if (is_mutation &&
        (options.actor.empty() || options.source.empty() ||
         options.reason.empty())) {
      throw std::runtime_error(
          "--actor, --source, and --reason are required for catalog mutations");
    }
    if (!is_mutation &&
        (!options.actor.empty() || !options.source.empty() ||
         !options.reason.empty())) {
      throw std::runtime_error(
          "review provenance is only valid for catalog mutations");
    }
    if (!is_mutation) {
      if (has_catalog_identity_options || !options.provider.empty()) {
        throw std::runtime_error(
            "identity options are only valid for catalog add or map");
      }
      return options;
    }
    if (!options.catalog_entity) {
      throw std::runtime_error("--entity is required for catalog mutations");
    }
    if (!options.has_canonical_id || options.canonical_id <= 0) {
      throw std::runtime_error("a positive --canonical-id is required");
    }
    const bool has_authoring_fields =
        options.has_competition_id || !options.competition.empty() ||
        !options.season.empty() || options.has_home_team_id ||
        options.has_away_team_id || options.kickoff_seconds ||
        options.home_score || options.away_score;
    const bool has_provider_identity =
        !options.provider.empty() || !options.provider_id.empty() ||
        options.provider_match_id;
    if (options.command == Command::CatalogRename ||
        options.command == Command::CatalogDeprecate ||
        options.command == Command::CatalogMerge) {
      if (*options.catalog_entity == CatalogEntityType::Match) {
        throw std::runtime_error(
            "catalog maintenance supports competition, season, team, or player");
      }
      if (has_authoring_fields || has_provider_identity) {
        throw std::runtime_error(
            "authoring and provider options are not valid for catalog maintenance");
      }
      if (options.command == Command::CatalogRename) {
        if (options.name.empty() || options.has_target_canonical_id) {
          throw std::runtime_error(
              "catalog rename requires --name and does not accept "
              "--target-canonical-id");
        }
      } else if (options.command == Command::CatalogDeprecate) {
        if (!options.name.empty() || options.has_target_canonical_id) {
          throw std::runtime_error(
              "catalog deprecate accepts no name or target canonical ID");
        }
      } else {
        if (!options.name.empty() || !options.has_target_canonical_id ||
            options.target_canonical_id <= 0 ||
            options.target_canonical_id == options.canonical_id) {
          throw std::runtime_error(
              "catalog merge requires a different positive "
              "--target-canonical-id");
        }
      }
      return options;
    }
    if (options.command == Command::CatalogAdd) {
      if (!options.provider.empty() || !options.provider_id.empty() ||
          options.provider_match_id || options.has_target_canonical_id) {
        throw std::runtime_error(
            "provider identity options are only valid for catalog map");
      }
      if (*options.catalog_entity == CatalogEntityType::Match) {
        if (!options.name.empty() || options.has_competition_id ||
            options.competition.empty() || options.season.empty() ||
            !options.has_home_team_id || options.home_team_id <= 0 ||
            !options.has_away_team_id || options.away_team_id <= 0) {
          throw std::runtime_error(
              "catalog add match requires --competition, --season, and positive "
              "--home-team-id and --away-team-id");
        }
        if (options.home_score.has_value() !=
            options.away_score.has_value()) {
          throw std::runtime_error(
              "--home-score and --away-score must be specified together");
        }
      } else {
        if (options.name.empty()) {
          throw std::runtime_error("--name is required for this catalog entity");
        }
        if (!options.competition.empty() || !options.season.empty() ||
            options.has_home_team_id || options.has_away_team_id ||
            options.kickoff_seconds || options.home_score ||
            options.away_score) {
          throw std::runtime_error(
              "match fields are only valid when adding a canonical match");
        }
        if (*options.catalog_entity == CatalogEntityType::Season) {
          if (!options.has_competition_id || options.competition_id <= 0) {
            throw std::runtime_error(
                "catalog add season requires a positive --competition-id");
          }
        } else if (options.has_competition_id) {
          throw std::runtime_error(
              "--competition-id is only valid when adding a canonical season");
        }
      }
      return options;
    }
    if (*options.catalog_entity == CatalogEntityType::Match) {
      throw std::runtime_error(
          "provider match mappings are created by reconciliation decisions");
    }
    if (options.provider.empty() || options.provider_id.empty()) {
      throw std::runtime_error(
          "--provider and --provider-id are required for catalog map");
    }
    if (!options.name.empty() || options.has_competition_id ||
        options.has_target_canonical_id ||
        !options.competition.empty() || !options.season.empty() ||
        options.has_home_team_id || options.has_away_team_id ||
        options.kickoff_seconds || options.home_score ||
        options.away_score) {
      throw std::runtime_error(
          "canonical entity fields are only valid for catalog add");
    }
    if (options.provider_match_id &&
        *options.catalog_entity != CatalogEntityType::Team &&
        *options.catalog_entity != CatalogEntityType::Player) {
      throw std::runtime_error(
          "--provider-match-id is only valid for team or player mappings");
    }
    return options;
  }

  if (!options.manifest.empty() || !options.store.empty() ||
      options.dry_run) {
    throw std::runtime_error(
        "--manifest, --store, and --dry-run are only valid for catalog "
        "import");
  }

  const bool is_reconciliation =
      options.command != Command::Import && options.command != Command::Query;
  if (is_reconciliation) {
    if (has_catalog_identity_options) {
      throw std::runtime_error(
          "catalog identity options are only valid for catalog commands");
    }
    const bool has_event_option =
        !options.provider.empty() || options.has_match_id ||
        !options.input.empty() || !options.output.empty() ||
        !options.database.empty() || options.has_limit ||
        !options.filters.empty() || !options.projection.empty() ||
        !options.group_by.empty() || !options.aggregates.empty() ||
        options.home_first_half_direction.has_value();
    if (has_event_option) {
      throw std::runtime_error(
          "event import and query options are not valid for reconcile commands");
    }
    if (options.review.empty()) {
      throw std::runtime_error("--review is required for reconcile commands");
    }
    if (options.command == Command::ReconcileGenerate) {
      if (options.left_provider.empty() || options.left_input.empty() ||
          options.right_provider.empty() || options.right_input.empty()) {
        throw std::runtime_error(
            "reconcile generate requires both provider and input pairs");
      }
    } else if (!options.left_provider.empty() || !options.left_input.empty() ||
               !options.right_provider.empty() ||
               !options.right_input.empty()) {
      throw std::runtime_error(
          "provider and input pairs are only valid for reconcile generate");
    }
    if (options.command == Command::ReconcileInspect ||
        options.command == Command::ReconcileAccept ||
        options.command == Command::ReconcileReject) {
      if (!options.has_candidate_id || options.candidate_id == 0) {
        throw std::runtime_error("a positive --candidate-id is required");
      }
    } else if (options.has_candidate_id) {
      throw std::runtime_error(
          "--candidate-id is only valid for reconcile inspect, accept, or reject");
    }
    if (options.command == Command::ReconcileAccept &&
        (!options.has_canonical_match_id || options.canonical_match_id <= 0)) {
      throw std::runtime_error("a positive --canonical-match-id is required");
    } else if (options.command != Command::ReconcileAccept &&
               options.has_canonical_match_id) {
      throw std::runtime_error(
          "--canonical-match-id is only valid for reconcile accept");
    }
    const bool is_decision = options.command == Command::ReconcileAccept ||
                             options.command == Command::ReconcileReject;
    if (is_decision && (options.actor.empty() || options.source.empty() ||
                        options.reason.empty())) {
      throw std::runtime_error(
          "--actor, --source, and --reason are required for reconcile decisions");
    } else if (!is_decision &&
               (!options.actor.empty() || !options.source.empty() ||
                !options.reason.empty())) {
      throw std::runtime_error(
          "--actor, --source, and --reason are only valid for reconcile decisions");
    }
    if (options.command != Command::ReconcileList &&
        options.candidate_status) {
      throw std::runtime_error("--status is only valid for reconcile list");
    }
    return options;
  }

  if (!options.review.empty() || !options.left_provider.empty() ||
      !options.left_input.empty() || !options.right_provider.empty() ||
      !options.right_input.empty() || options.has_candidate_id ||
      options.has_canonical_match_id || options.candidate_status ||
      !options.actor.empty() || !options.source.empty() ||
      !options.reason.empty() || has_catalog_identity_options) {
    throw std::runtime_error(
        "reconciliation options are only valid for reconcile commands");
  }
  const bool has_complete_raw_source =
      !options.provider.empty() && options.has_match_id && !options.input.empty();
  const bool has_any_raw_source =
      !options.provider.empty() || options.has_match_id || !options.input.empty();
  if (options.command == Command::Import) {
    if (!has_complete_raw_source) {
      throw std::runtime_error(
          "--provider, --match-id, and --input are required for import");
    }
    if (!options.database.empty()) {
      throw std::runtime_error("--database is only valid for query");
    }
    if (!options.filters.empty() || !options.projection.empty() ||
        !options.group_by.empty() || !options.aggregates.empty()) {
      throw std::runtime_error(
          "--filter, --project, --group-by, and --aggregate are only valid for query");
    }
  } else {
    if (!options.output.empty()) {
      throw std::runtime_error("--output is only valid for import");
    }
    if (!options.database.empty()) {
      if (has_any_raw_source) {
        throw std::runtime_error(
            "--database cannot be combined with --provider, --match-id, or --input");
      }
    } else if (!has_complete_raw_source) {
      throw std::runtime_error(
          "query requires --database or --provider, --match-id, and --input");
    }
    if (options.projection.empty() == options.aggregates.empty()) {
      throw std::runtime_error(
          "query requires exactly one of --project or --aggregate");
    }
    if (!options.projection.empty() && !options.group_by.empty()) {
      throw std::runtime_error("--group-by requires --aggregate");
    }
    if (options.has_limit) {
      throw std::runtime_error("--limit is only valid for import");
    }
  }
  if (options.home_first_half_direction && options.provider != "metrica") {
    throw std::runtime_error(
        "--home-first-half-direction is only valid with --provider metrica");
  }
  if (options.provider == "metrica" && has_any_raw_source &&
      !options.home_first_half_direction) {
    throw std::runtime_error(
        "--provider metrica requires --home-first-half-direction");
  }
  return options;
}

EventQuery makeEventQuery(const Options& options) {
  EventQuery query;
  query.projection = parseProjection(options.projection);
  query.filters.reserve(options.filters.size());
  for (const auto& filter : options.filters) {
    query.filters.push_back(parseFilter(filter));
  }
  return query;
}

AggregationQuery makeAggregationQuery(const Options& options) {
  AggregationQuery query;
  if (!options.group_by.empty()) {
    query.group_by = parseProjection(options.group_by);
  }
  query.filters.reserve(options.filters.size());
  for (const auto& filter : options.filters) {
    query.filters.push_back(parseFilter(filter));
  }
  query.aggregates.reserve(options.aggregates.size());
  for (const auto& aggregate : options.aggregates) {
    query.aggregates.push_back(parseAggregate(aggregate));
  }
  return query;
}

}  // namespace emberdb::cli
