#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "emberdb/common/football_event.h"
#include "emberdb/identity/canonical_identity.h"

namespace emberdb {

class FootballEventTable;

// Missing means that a provider identity was present on the event but had no
// explicit canonical mapping. It is intentionally distinct from Absent.
enum class EventIdentityStatus { Absent, Resolved, Missing };

struct EventIdentityCoverageCounts {
  std::size_t present{};
  std::size_t resolved{};
  std::size_t missing{};
  std::size_t absent{};

  bool operator==(const EventIdentityCoverageCounts&) const = default;
};

struct EventIdentityDiagnostic {
  std::size_t event_index{};
  std::string source;
  std::string provider;
  std::string provider_event_id;
  Identifier provider_match_id{};
  std::optional<Identifier> provider_team_id;
  std::optional<std::string> provider_team_name;
  std::optional<Identifier> provider_player_id;
  std::optional<std::string> provider_player_name;

  EventIdentityStatus match_status{EventIdentityStatus::Missing};
  EventIdentityStatus team_status{EventIdentityStatus::Absent};
  EventIdentityStatus player_status{EventIdentityStatus::Absent};
  CanonicalEventIdentity canonical_identity;

  bool operator==(const EventIdentityDiagnostic& other) const {
    return event_index == other.event_index && source == other.source &&
           provider == other.provider &&
           provider_event_id == other.provider_event_id &&
           provider_match_id == other.provider_match_id &&
           provider_team_id == other.provider_team_id &&
           provider_team_name == other.provider_team_name &&
           provider_player_id == other.provider_player_id &&
           provider_player_name == other.provider_player_name &&
           match_status == other.match_status &&
           team_status == other.team_status &&
           player_status == other.player_status &&
           canonical_identity.match_id == other.canonical_identity.match_id &&
           canonical_identity.team_id == other.canonical_identity.team_id &&
           canonical_identity.player_id == other.canonical_identity.player_id;
  }
};

struct EventIdentityCoverageReport {
  std::string source;
  std::size_t total_events{};
  EventIdentityCoverageCounts matches;
  EventIdentityCoverageCounts teams;
  EventIdentityCoverageCounts players;
  std::vector<EventIdentityDiagnostic> events;

  bool operator==(const EventIdentityCoverageReport&) const = default;
};

// Inspects events in table order. The function does not mutate the table or
// catalog, and source is copied into every diagnostic to retain file context.
[[nodiscard]] EventIdentityCoverageReport analyzeEventIdentityCoverage(
    const FootballEventTable& table, const CanonicalIdentityCatalog& catalog,
    std::string source = {});

}  // namespace emberdb
