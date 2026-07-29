#include "cli/renderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include "emberdb/common/football_event.h"
#include "emberdb/common/football_event_column.h"
#include "emberdb/reconciliation/match_reconciliation.h"

namespace emberdb::cli {
namespace {

std::string optionalText(const std::optional<std::string>& value) {
  return value.value_or("NULL");
}

std::string coordinateText(const std::optional<Coordinate>& value) {
  if (!value) {
    return "NULL";
  }
  return "(" + std::to_string(value->x) + ", " + std::to_string(value->y) +
         ")";
}

void printPreview(std::ostream& output, const FootballEventTable& table,
                  std::size_t limit) {
  if (limit == 0) {
    return;
  }
  output << "\nPreview\n";
  for (std::size_t index = 0; index < std::min(limit, table.rowCount());
       ++index) {
    const auto event = table.row(index);
    output << index << ": id=" << event.provider_event_id
           << " type=" << event.event_type
           << " team=" << optionalText(event.team_name)
           << " player=" << optionalText(event.player_name)
           << " start=" << coordinateText(event.start_location)
           << " end=" << coordinateText(event.end_location)
           << " source_start=" << coordinateText(event.source_start_location)
           << " source_end=" << coordinateText(event.source_end_location)
           << '\n';
  }
}

template <typename Cell>
std::string queryValueText(const Cell& cell) {
  if (!cell) {
    return "NULL";
  }
  return std::visit(
      [](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return value;
        } else if constexpr (std::is_same_v<Value,
                                            std::chrono::milliseconds>) {
          return std::to_string(value.count());
        } else {
          std::ostringstream output;
          output << value;
          return output.str();
        }
      },
      *cell);
}

std::string providerMatchText(const ProviderMatchReference& reference) {
  return reference.provider + ":" + reference.id;
}

std::string_view evidenceStatusText(ReconciliationStatus status) {
  switch (status) {
    case ReconciliationStatus::Missing:
      return "missing";
    case ReconciliationStatus::Agreeing:
      return "agreeing";
    case ReconciliationStatus::Conflicting:
      return "conflicting";
    case ReconciliationStatus::Uncertain:
      return "uncertain";
  }
  return "unknown";
}

void printCandidateSummary(std::ostream& output,
                           const MatchCandidateRecord& candidate) {
  output << candidate.id << '\t' << matchCandidateStatusName(candidate.status)
         << '\t' << candidate.reconciliation.confidence << '\t'
         << providerMatchText(candidate.reconciliation.left_match) << '\t'
         << providerMatchText(candidate.reconciliation.right_match) << '\n';
}

void printEvidence(std::ostream& output, std::string_view name,
                   const MatchFieldEvidence& evidence) {
  output << name << '\t' << evidenceStatusText(evidence.status) << '\t'
         << optionalText(evidence.left_value) << '\t'
         << optionalText(evidence.right_value) << '\t'
         << optionalText(evidence.canonical_value) << '\n';
}

}  // namespace

void printUsage(std::ostream& output) {
  output << "Usage: emberdb_cli import --provider PROVIDER --match-id ID --input PATH "
            "[--home-first-half-direction left-to-right|right-to-left] "
            "[--output DATABASE] [--limit N]\n"
            "       emberdb_cli query (--database DATABASE | --provider PROVIDER "
            "--match-id ID --input PATH) "
            "(--project COLUMN[,COLUMN...] | --aggregate FUNCTION(COLUMN|*)) "
            "[--aggregate FUNCTION(COLUMN|*)]... [--group-by COLUMN[,COLUMN...]] "
            "[--filter COLUMN=VALUE]...\n"
            "       emberdb_cli reconcile generate --review PATH "
            "--left-provider PROVIDER --left-input PATH "
            "--right-provider PROVIDER --right-input PATH\n"
            "       emberdb_cli reconcile list --review PATH "
            "[--status unresolved|accepted|rejected]\n"
            "       emberdb_cli reconcile inspect --review PATH --candidate-id ID\n"
            "       emberdb_cli reconcile accept --review PATH --candidate-id ID "
            "--canonical-match-id ID --actor TEXT --source TEXT --reason TEXT\n"
            "       emberdb_cli reconcile reject --review PATH --candidate-id ID "
            "--actor TEXT --source TEXT --reason TEXT\n";
}

void printImportResult(std::ostream& output, const FootballEventTable& table,
                       const Options& options,
                       std::optional<std::uintmax_t> database_size) {
  output << "Imported " << table.rowCount() << " events\n"
         << "Provider: "
         << (table.rowCount() == 0 ? options.provider : table.row(0).provider)
         << '\n'
         << "Match ID: " << options.match_id << '\n'
         << "Columns: " << FootballEventTable::kColumnCount << '\n'
         << "Events with player data: " << table.playerDataCount() << '\n'
         << "Events with start locations: " << table.startLocationCount() << '\n'
         << "Events with end locations: " << table.endLocationCount() << '\n';
  if (database_size) {
    output << "Saved database: " << options.output.string() << '\n'
           << "Database size: " << *database_size << " bytes\n";
  }
  printPreview(output, table, options.limit);
}

void printAggregationResult(std::ostream& output,
                            const AggregationResult& result) {
  output << "Result rows: " << result.rowCount() << '\n';
  for (std::size_t column = 0; column < result.columnCount(); ++column) {
    if (column != 0) {
      output << '\t';
    }
    output << result.columnNames()[column];
  }
  output << '\n';
  for (std::size_t row = 0; row < result.rowCount(); ++row) {
    for (std::size_t column = 0; column < result.columnCount(); ++column) {
      if (column != 0) {
        output << '\t';
      }
      output << queryValueText(result.cell(row, column));
    }
    output << '\n';
  }
}

void printQueryResult(std::ostream& output, const EventQueryResult& result) {
  output << "Matched " << result.rowCount()
         << (result.rowCount() == 1 ? " event\n" : " events\n");
  for (std::size_t column = 0; column < result.columnCount(); ++column) {
    if (column != 0) {
      output << '\t';
    }
    output << columnName(result.columns()[column]);
  }
  output << '\n';
  for (std::size_t row = 0; row < result.rowCount(); ++row) {
    for (std::size_t column = 0; column < result.columnCount(); ++column) {
      if (column != 0) {
        output << '\t';
      }
      output << queryValueText(result.cell(row, column));
    }
    output << '\n';
  }
}

void printCandidateGeneration(std::ostream& output,
                              std::size_t generated_count,
                              std::size_t added_count,
                              const std::vector<std::uint64_t>& ids) {
  output << "Generated " << generated_count << " qualified "
         << (generated_count == 1 ? "comparison" : "comparisons") << '\n'
         << "Added " << added_count << " new "
         << (added_count == 1 ? "candidate" : "candidates") << '\n';
  if (!ids.empty()) {
    output << "Candidate IDs:";
    for (const auto id : ids) {
      output << ' ' << id;
    }
    output << '\n';
  }
}

void printCandidateList(
    std::ostream& output,
    const std::vector<const MatchCandidateRecord*>& candidates) {
  output << "Candidates: " << candidates.size() << '\n'
         << "id\tstatus\tconfidence\tleft_match\tright_match\n";
  for (const auto* candidate : candidates) {
    printCandidateSummary(output, *candidate);
  }
}

void printCandidateInspection(std::ostream& output,
                              const MatchCandidateRecord& candidate) {
  printCandidateSummary(output, candidate);
  if (candidate.accepted_match_id) {
    output << "Canonical match: " << candidate.accepted_match_id->value << '\n';
  }
  if (candidate.rejection_reason) {
    output << "Rejection reason: " << *candidate.rejection_reason << '\n';
  }
  if (candidate.decision_provenance) {
    output << "Decision actor: " << candidate.decision_provenance->actor << '\n'
           << "Decision source: " << candidate.decision_provenance->source
           << '\n'
           << "Decision reason: " << candidate.decision_provenance->reason
           << '\n'
           << "Decision recorded at: "
           << candidate.decision_provenance->recorded_at.time_since_epoch().count()
           << '\n';
  }
  output << "field\tstatus\tleft_value\tright_value\tcanonical_value\n";
  const auto& result = candidate.reconciliation;
  printEvidence(output, "competition", result.competition);
  printEvidence(output, "season", result.season);
  printEvidence(output, "kickoff", result.kickoff);
  printEvidence(output, "home_team", result.home_team);
  printEvidence(output, "away_team", result.away_team);
  printEvidence(output, "score", result.score);
}

void printCandidateAccepted(std::ostream& output, std::uint64_t candidate_id,
                            Identifier canonical_match_id) {
  output << "Accepted candidate " << candidate_id << " as canonical match "
         << canonical_match_id << '\n';
}

void printCandidateRejected(std::ostream& output, std::uint64_t candidate_id,
                            const std::string& reason) {
  output << "Rejected candidate " << candidate_id << ": " << reason << '\n';
}

}  // namespace emberdb::cli
