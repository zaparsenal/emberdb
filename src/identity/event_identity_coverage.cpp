#include "emberdb/identity/event_identity_coverage.h"

#include <stdexcept>
#include <utility>

#include "emberdb/storage/football_event_table.h"

namespace emberdb {
namespace {

EventIdentityStatus statusFor(bool present, bool resolved) {
  if (!present) {
    return EventIdentityStatus::Absent;
  }
  return resolved ? EventIdentityStatus::Resolved
                  : EventIdentityStatus::Missing;
}

void countStatus(EventIdentityCoverageCounts& counts,
                 EventIdentityStatus status) {
  if (status == EventIdentityStatus::Absent) {
    ++counts.absent;
    return;
  }

  ++counts.present;
  if (status == EventIdentityStatus::Resolved) {
    ++counts.resolved;
  } else {
    ++counts.missing;
  }
}

}  // namespace

EventIdentityCoverageReport analyzeEventIdentityCoverage(
    const FootballEventTable& table, const CanonicalIdentityCatalog& catalog,
    std::string source) {
  if (!table.validate()) {
    throw std::invalid_argument(
        "cannot analyze identity coverage for an inconsistent event table");
  }

  EventIdentityCoverageReport report;
  report.source = std::move(source);
  report.total_events = table.rowCount();
  report.events.reserve(report.total_events);

  for (std::size_t index = 0; index < report.total_events; ++index) {
    const auto event = table.row(index);
    const auto canonical_identity = catalog.resolveEvent(event);
    const bool team_present = event.team_id.has_value() ||
                              event.team_name.has_value();
    const bool player_present = event.player_id.has_value() ||
                                event.player_name.has_value();

    EventIdentityDiagnostic diagnostic;
    diagnostic.event_index = index;
    diagnostic.source = report.source;
    diagnostic.provider = event.provider;
    diagnostic.provider_event_id = event.provider_event_id;
    diagnostic.provider_match_id = event.match_id;
    diagnostic.provider_team_id = event.team_id;
    diagnostic.provider_team_name = event.team_name;
    diagnostic.provider_player_id = event.player_id;
    diagnostic.provider_player_name = event.player_name;
    diagnostic.match_status =
        statusFor(true, canonical_identity.match_id.has_value());
    diagnostic.team_status =
        statusFor(team_present, canonical_identity.team_id.has_value());
    diagnostic.player_status =
        statusFor(player_present, canonical_identity.player_id.has_value());
    diagnostic.canonical_identity = canonical_identity;

    countStatus(report.matches, diagnostic.match_status);
    countStatus(report.teams, diagnostic.team_status);
    countStatus(report.players, diagnostic.player_status);
    report.events.push_back(std::move(diagnostic));
  }

  return report;
}

}  // namespace emberdb
