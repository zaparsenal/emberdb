# EmberDB

EmberDB is a local columnar analytics engine specialized for football event data. It is an educational but serious C++ systems project for learning ingestion, schema normalization, column-oriented storage, and query execution without trying to replace a general-purpose analytical database.

## What works today

Implemented milestones include:

- a provider-independent, typed `FootballEvent` model;
- typed `CanonicalMatch`, `CanonicalTeam`, and `CanonicalPlayer` catalogs with
  provider-to-canonical ID mappings;
- a provider adapter interface with StatsBomb Open Data JSON, Metrica Sports CSV, and
  the open Wyscout research JSON adapters;
- metadata adapters for StatsBomb matches/lineups and the open Wyscout competition,
  team, player, and match catalogs;
- deterministic match reconciliation candidates with per-field provenance, status, and
  confidence;
- safe preservation of missing possession, team, player, outcome, and coordinate values;
- a typed 22-column in-memory `FootballEventTable` with row reconstruction and consistency validation;
- canonical 0–100 by 0–100 coordinates with attacks oriented left to right;
- preserved provider coordinates for traceability and provider-specific bounds validation;
- provider-neutral typed equality filters and projections with explicit null results;
- deterministic query execution that preserves imported event order;
- typed `COUNT`, `SUM`, `AVG`, `MIN`, and `MAX` aggregation with optional grouping;
- a versioned, checksummed columnar `.ember` file containing the normalized schema;
- exact persistence of typed columns and explicit null bitmaps;
- an `import` CLI with deterministic summary counts and optional preview;
- a `query` CLI that translates filters, projections, aggregates, and grouping into the
  programmatic query APIs; and
- offline unit fixtures covering ingestion failures, coordinates, nulls, table behavior, and queries.

SQL, compression, broader analytical expressions, automatic cross-provider entity
matching, and event reconciliation are not implemented.

## Architecture

```text
StatsBomb JSON --> StatsBombEventAdapter --> validation/normalization --+
                                                                    |
Metrica CSV ----> MetricaEventAdapter ----> validation/normalization --+
                                                                    |
Wyscout JSON ---> WyscoutEventAdapter ---> validation/normalization --+
                                                                    |
                                                                    v
provider-independent FootballEvent values (import once)
        |
        v
in-memory FootballEventTable (one typed vector per field)
        |
        +----> versioned .ember columnar file
        |              |
        |              +----> validated reload without provider files
        |
        v
typed filters, projections, aggregations, and grouping
        |
        v
CLI tabular results

StatsBomb match/lineup JSON ---> StatsBombMetadataAdapter --+
                                                            |
Wyscout metadata JSON ----------> WyscoutMetadataAdapter ----+--> provider metadata
                                                            |          |
explicit provider ID mappings ------------------------------+          v
                                                    CanonicalIdentityCatalog
                                                            |
normalized FootballEvent -----------------------------------+--> canonical event identity
provider match metadata + canonical team mappings ----------+--> ranked match candidates
```

Raw provider files are confined to ingestion adapters. Storage and query execution
accept only normalized events and do not depend on StatsBomb, Metrica, or Wyscout
formats.

The current 22 logical columns are provider event ID, match ID, period, timestamp,
minute, second, possession ID, team ID/name, player ID/name, event type, outcome,
normalized start x/y, normalized end x/y, provider, source start x/y, and source end x/y.

## Canonical identity catalogs

`CanonicalIdentityCatalog` stores typed canonical match, team, and player records
separately from provider metadata. A provider identity can map to only one canonical ID,
while several provider identities may map to the same canonical entity. Conflicting
remaps and mappings to unknown canonical records are rejected.

StatsBomb metadata ingestion reads competition/season, kickoff, home/away teams, scores,
and lineup players. Wyscout metadata ingestion reads its separate competition, team,
player, and match exports. Missing source kickoff, score, or current-team values stay
optional. The open Wyscout player file's JSON null and literal `"null"` current-team
representations are both treated as missing.

Metrica has no stable team IDs in its standard anonymized CSV. Call
`mapMetricaTeams(provider_match_id, home_team_id, away_team_id)` to map its repeating
`Home` and `Away` labels within one match. They are deliberately never global mappings.

`resolveEvent` returns a `CanonicalEventIdentity` alongside an existing normalized
event; it does not overwrite the event's provider IDs. The catalog and metadata adapters
are currently programmatic APIs declared under `include/emberdb/identity` and
`include/emberdb/ingestion`. Catalog persistence, automatic candidate acceptance, and
CLI mapping commands are planned rather than implied.

## Match reconciliation

`reconcileMatches` compares two provider match records without changing either record or
adding a canonical mapping. Each comparison retains the left and right provider/value
and classifies competition, season, kickoff, home team, away team, and score as
`Missing`, `Agreeing`, `Conflicting`, or `Uncertain`.

Team agreement requires existing provider-to-canonical team mappings. Scores compare
exactly. Kickoffs agree within five minutes, are uncertain through 24 hours, and conflict
beyond that by default. Cross-provider competition and season names use only
case-insensitive, whitespace-normalized equality; unequal names remain uncertain rather
than being fuzzily guessed. Same-provider IDs compare exactly.

The confidence weights are home team 0.25, away team 0.25, kickoff 0.20, score 0.15,
competition 0.10, and season 0.05. Agreeing fields receive full weight, uncertain fields
half weight, and missing or conflicting fields none. The default candidate threshold is
0.70. Team, kickoff, or score conflicts always disqualify a candidate regardless of its
numeric score. `findMatchCandidates` returns only qualified candidates, ordered by
confidence with deterministic provider-ID tie breaking.

This is candidate generation, not automatic acceptance. EmberDB does not create a
`CanonicalMatch`, overwrite source values, or add match mappings from a reconciliation
result.

## Requirements and build

- C++20 compiler
- CMake 3.20 or newer
- network access on the first configure so CMake can fetch pinned nlohmann/json and GoogleTest releases

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

For a release build, configure a separate directory with `-DCMAKE_BUILD_TYPE=Release`.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Tests use only files under `tests/fixtures` and do not access the internet. The first CMake configuration may download build dependencies.

## CLI usage

Import and preview events:

```bash
./build/emberdb_cli import \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --limit 2
```

Example output:

```text
Imported 2 events
Provider: StatsBomb
Match ID: 12345
Columns: 22
Events with player data: 2
Events with start locations: 2
Events with end locations: 2

Preview
0: id=evt-pass-1 type=Pass team=Ember FC player=Alex Forward start=(35.416667, 39.062500) end=(59.166667, 28.125000) source_start=(42.500000, 31.250000) source_end=(71.000000, 22.500000)
1: id=evt-carry-2 type=Carry team=Ember FC player=Alex Forward start=(59.166667, 28.125000) end=(65.000000, 36.250000) source_start=(71.000000, 22.500000) source_end=(78.000000, 29.000000)
```

`--match-id` is required because source event files do not always carry usable match
context. Provider names are selected case-sensitively; `statsbomb`, `metrica`, and
`wyscout` are accepted.

Import Metrica's standard event CSV:

```bash
./build/emberdb_cli import \
  --provider metrica \
  --match-id 1 \
  --home-first-half-direction left-to-right \
  --input Sample_Game_1_RawEventsData.csv \
  --output sample-game-1.ember
```

Metrica uses fixed camera-oriented coordinates, and its public sample matches do not
share one home-team first-half direction. The required direction option is therefore
explicit import metadata; EmberDB does not guess it from shots, kickoff events, or player
positions. Accepted values are `left-to-right` and `right-to-left`. The option is invalid
for providers whose files already express attacking direction.

Import one match from an open Wyscout competition event file:

```bash
./build/emberdb_cli import \
  --provider wyscout \
  --match-id 2576335 \
  --input events_Italy.json \
  --output match-2576335.ember
```

The open Wyscout files contain an entire competition, so the adapter selects only events
whose `matchId` equals `--match-id`. It fails if the file contains no events for that
match. The adapter targets the CC BY 4.0 Soccer match event dataset published on Figshare,
not Wyscout's current commercial API.

Import once into an EmberDB database:

```bash
./build/emberdb_cli import \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --output match.ember
```

The CLI reports the resulting file size. Existing database files are never overwritten;
choose a new output path or remove the old file explicitly.

Query the saved database without reparsing provider JSON:

```bash
./build/emberdb_cli query \
  --database match.ember \
  --filter event_type=Pass \
  --project player_name,minute,start_x,start_y,source_start_x,source_start_y
```

`--database` can be used with projection queries and grouped aggregation queries. It is
mutually exclusive with `--provider`, `--match-id`, and `--input`.

Filter and project events:

```bash
./build/emberdb_cli query \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --filter event_type=Pass \
  --project player_name,minute,start_x,start_y,source_start_x,source_start_y
```

Example output (columns are tab-separated):

```text
Matched 1 event
player_name    minute  start_x  start_y  source_start_x  source_start_y
Alex Forward   12      35.4167  39.0625  42.5            31.25
```

`--filter` implements typed equality and may be repeated; repeated filters use `AND`
semantics. Values are parsed according to the selected column, so `minute=12` is an
integer comparison while `event_type=Pass` is a text comparison. Timestamp filter
values are milliseconds. Projected nulls print as `NULL`, result rows retain source
order, and duplicate projection columns are rejected.

The stable query column names are: `provider_event_id`, `match_id`, `period`,
`timestamp`, `minute`, `second`, `possession_id`, `team_id`, `team_name`, `player_id`,
`player_name`, `event_type`, `outcome`, `start_x`, `start_y`, `end_x`, `end_y`,
`provider`, `source_start_x`, `source_start_y`, `source_end_x`, and `source_end_y`.

The query API is declared in `include/emberdb/query/event_query.h`. It accepts typed
`EqualityPredicate` values rather than strings; the CLI is only a translation boundary.

Aggregate and group events:

```bash
./build/emberdb_cli query \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --group-by event_type \
  --aggregate 'count(*)' \
  --aggregate 'avg(start_x)'
```

Example output (columns are tab-separated):

```text
Result rows: 2
event_type  count(*)  avg(start_x)
Pass        1         35.4167
Carry       1         59.1667
```

Supported aggregate expressions are `count(*)`, `count(column)`, `sum(column)`,
`avg(column)`, `min(column)`, and `max(column)`. `SUM` and `AVG` accept integer and
numeric columns; `MIN` and `MAX` accept every column type. `--group-by` accepts one or
more comma-separated columns. Existing `--filter` predicates run before grouping.

`count(*)` counts matching rows, while `count(column)` ignores nulls. Other aggregates
also ignore null inputs and return `NULL` when no non-null value exists. Null grouping
keys form a group, and groups appear in first-seen source order. A global aggregation
over no matching rows returns one row (`count(*)` is zero); a grouped aggregation over
no matching rows returns no rows.

The programmatic aggregation API is declared in
`include/emberdb/query/aggregation_query.h`.

## Persistent file format

Each `.ember` database is one table in one container file. Format version 2 uses a
fixed little-endian header containing magic bytes, the format version, flags, row count,
and a 22-entry schema directory. Every directory entry records the stable column ID,
physical type, nullability, offsets, sizes, and CRC32 checksums for its null bitmap and
data payload.

Nullable columns store one presence bit per row (`1` means present). Only present values
are written to the typed payload. Identifiers and timestamps use 64-bit values;
period, minute, and second use 32-bit values; coordinates use IEEE 754 binary64; and
strings use a 64-bit byte length followed by their bytes. The format stores normalized
provider-neutral columns, never raw provider JSON or CSV.

Loading validates the magic, exact format version and schema, flags, canonical segment
layout, file bounds, bitmap sizes and unused bits, payload sizes, checksums, string
lengths, coordinate nullability, and reconstructed column lengths. Unsupported versions,
truncated files, corruption, trailing data, and schema mismatches fail with file and
column context. Writes use a temporary sibling and rename only after the complete file
has been written.

Version 2 adds canonical and source-coordinate columns. Earlier version 1 files are
rejected rather than guessed or silently migrated; reimport the provider source to create
a version 2 database.

## Data and coordinate semantics

StatsBomb timestamps are parsed into millisecond durations relative to the event period. The provider's `minute` and `second` fields are also retained as typed values.

`start_x`, `start_y`, `end_x`, and `end_y` use EmberDB's canonical pitch: both axes
range from 0 through 100, and the attacking direction runs left to right. The x-axis is
pitch length and the y-axis is pitch width. Boundaries are inclusive.

The StatsBomb adapter validates its 120 by 80 source pitch and transforms coordinates as
`x / 120 * 100` and `y / 80 * 100`. StatsBomb event coordinates are treated as already
oriented from the attacking team's goal toward the opponent's goal, so they require no
direction flip. A provider whose events attack right to left flips normalized x with
`100 - x`; normalized y is unchanged.

The Metrica standard CSV adapter validates its fixed 0–1 coordinates and scales each axis
by 100. It uses the imported home-team first-half direction, the event's `Home` or `Away`
team, and the period to orient every event left to right. Metrica uses `NaN` pairs for
missing locations. Some locations in the provider's public samples legitimately fall
just beyond the touchline or goal line; EmberDB preserves those finite values in the
source columns while leaving the corresponding canonical location null. Partially
missing pairs and non-finite numeric values are rejected.

Metrica standard CSV rows have no provider event ID. EmberDB derives a deterministic ID
from the imported match ID, start frame, and zero-based event order. Its anonymized
`Home`/`Away` and `PlayerN` labels map to the name columns; team and player ID columns stay
null. Event types are converted from provider uppercase to stable title case, so a
Metrica `PASS` and a StatsBomb `Pass` can both be queried as `event_type=Pass`. The
provider subtype is retained in `outcome` because the current normalized model has no
separate subtype column.

The open Wyscout research export already uses a 0–100 pitch from the attacking team's
perspective, so canonical and source coordinates are equal. Its numeric event, match,
team, and player identifiers map directly into the normalized model; a documented
`playerId` of zero is treated as missing. Since names live in separate dataset files,
team and player names remain null in this event-only adapter. `eventSec` is retained as
period-relative milliseconds, while `minute` is derived with regulation and extra-time
period offsets. Tag 1801 maps to the normalized outcome `Accurate`; other tags are not
guessed into outcome values.

`source_start_x`, `source_start_y`, `source_end_x`, and `source_end_y` retain the exact
provider coordinates. Together with `provider`, they identify how each normalized value
was produced. Missing source locations produce missing normalized and source columns.
Non-finite or out-of-bounds provider coordinates fail ingestion rather than being
clamped or silently discarded, except for the documented Metrica off-pitch location
case above.

Pass and carry end locations are supported. Outcomes are extracted from common StatsBomb detail objects (`pass`, `shot`, `duel`, `dribble`, and `goalkeeper`) when present.

## Current limitations

- Version 1 files are uncompressed and are loaded fully into memory; there is no metadata
  pruning, streaming scan, schema migration, or partial-column loader yet.
- Equality is the only filter operation; there is no ordering, result limiting, SQL,
  optimizer, general expression evaluation, or distinct aggregation yet.
- No compression, dictionary encoding, parallelism, SIMD, or memory mapping is used.
- Only one event source file and one explicit match ID can be imported per invocation.
- Metrica's standard CSV adapter does not yet read its newer Game 3 JSON/FIFA package,
  tracking data, team sheets, or player metadata.
- The Wyscout event CLI reads the open 2019 research export only and does not
  automatically join the separately supported player, team, competition, or match
  metadata files.
- Canonical identity catalogs are in-memory programmatic APIs. They are not stored in
  `.ember` files, and EmberDB does not automatically decide that two provider entities
  are the same.
- Match reconciliation is a programmatic, pairwise candidate generator. There is no
  persisted review/acceptance workflow, competition mapping catalog, or command-line
  interface yet.
- Outcome extraction is intentionally limited to known common detail objects; the raw provider event is not retained.

## Long-term direction

The intended system evolves from provider adapters to normalized events, columnar persistence, a limited SQL parser and planner, execution operators, and terminal/CSV/JSON output. Additional providers should be added only through adapters, never by leaking their raw schemas into storage.

The recommended next milestone is a durable review-and-accept workflow for match
candidates: persist canonical catalogs and explicit accepted mappings, retain rejected
or unresolved candidates, and never auto-accept conflicts. Event reconciliation should
remain deferred until those match decisions survive reloads. SQL is also deliberately
deferred; when resumed, it should translate into the existing typed operations rather
than bypassing them.
