# EmberDB

EmberDB is a local columnar analytics engine specialized for football event data. It is an educational but serious C++ systems project for learning ingestion, schema normalization, column-oriented storage, and query execution without trying to replace a general-purpose analytical database.

## What works today

Implemented milestones include:

- a provider-independent, typed `FootballEvent` model;
- typed canonical competition, season, match, team, and player catalogs with
  provider-to-canonical ID mappings;
- a provider adapter interface with StatsBomb Open Data JSON, Metrica Sports CSV, and
  the open Wyscout research JSON adapters;
- metadata adapters for StatsBomb matches/lineups and the open Wyscout competition,
  team, player, and match catalogs;
- deterministic match reconciliation candidates with per-field provenance, status, and
  confidence;
- a durable review store for canonical catalogs, provider mappings, generated match
  candidates, evidence, and accepted or rejected decisions;
- reconciliation CLI commands to generate, list, inspect, accept, and reject match
  candidates;
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
                                                                       |
                                                                       v
                                                    versioned match review store
                                                    (unresolved/accepted/rejected)
```

Raw provider files are confined to ingestion adapters. Storage and query execution
accept only normalized events and do not depend on StatsBomb, Metrica, or Wyscout
formats.

The CLI is an internal composition layer rather than part of the database library.
Command parsing and typed query construction, execution orchestration, and terminal
rendering live in separate `src/cli` components; `src/main.cpp` only handles process
arguments, top-level errors, and exit status.

The current 22 logical columns are provider event ID, match ID, period, timestamp,
minute, second, possession ID, team ID/name, player ID/name, event type, outcome,
normalized start x/y, normalized end x/y, provider, source start x/y, and source end x/y.

Every normalized event passes the same provider-neutral validation before it leaves an
adapter, enters a `FootballEventTable`, or is accepted from a persisted `.ember` file.
Required text must not be blank; match and present provider IDs must be positive; period
and match time values must be valid; canonical coordinates must be within the normalized
pitch; and source coordinates must be finite. Adapters additionally enforce their own
source-schema and coordinate-system rules.

## Canonical identity catalogs

`CanonicalIdentityCatalog` stores typed canonical competition, season, match, team, and
player records separately from provider metadata. A canonical season belongs to an
existing canonical competition. A provider identity can map to only one canonical ID,
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
event; it does not overwrite the event's provider IDs. Audited catalog construction and
mapping APIs live on `MatchReviewStore`; each mutation records actor, source, reason,
timestamp, and revision. A complete catalog and its audit records can be saved in the
separate match review file. It is never embedded in an event `.ember` file.

## Match reconciliation

`reconcileMatches` compares two provider match records without changing either record or
adding a canonical mapping. Each comparison retains the left and right provider/value
and classifies competition, season, kickoff, home team, away team, and score as
`Missing`, `Agreeing`, `Conflicting`, or `Uncertain`.

Team agreement requires existing provider-to-canonical team mappings. Explicit
competition and season mappings take precedence over provider labels. Without mappings,
cross-provider competition and season names use only case-insensitive,
whitespace-normalized equality; unequal names remain uncertain rather than being fuzzily
guessed. Same-provider IDs compare exactly. Scores compare exactly. Kickoffs agree within
five minutes, are uncertain through 24 hours, and conflict beyond that by default.

The confidence weights are home team 0.25, away team 0.25, kickoff 0.20, score 0.15,
competition 0.10, and season 0.05. Agreeing fields receive full weight, uncertain fields
half weight, and missing or conflicting fields none. The default candidate threshold is
0.70. Team, kickoff, score, or same-provider competition/season ID conflicts always
disqualify a candidate regardless of its numeric score. Resolving both match sides to
one canonical team is also a conflict. `findMatchCandidates` returns only qualified
candidates, ordered by confidence with deterministic provider-ID tie breaking.

Candidate generation never accepts automatically. A reviewer can retain a candidate as
`unresolved`, reject it with required provenance, or accept it against an existing
`CanonicalMatch` with the same actor/source/reason/timestamp record. Acceptance adds both
provider-match-to-canonical-match mappings to the catalog. Repeating the identical
acceptance or rejection is safe. Attempting to reverse a finalized decision, change its
canonical match or rejection reason, or conflict with an existing provider match mapping
fails with a clear error.

Generated confidence and all six per-field evidence records remain fixed in the audit
record. Regenerating an existing ordered provider pair reuses its candidate ID and does
not overwrite prior evidence or decisions.

## Requirements and build

- C++20 compiler
- CMake 3.20 or newer
- network access on the first configure so CMake can fetch pinned nlohmann/json and GoogleTest releases

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

For a release build, configure a separate directory with `-DCMAKE_BUILD_TYPE=Release`.

AddressSanitizer and UndefinedBehaviorSanitizer can be enabled with Clang or GCC:

```bash
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DEMBERDB_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

GitHub Actions configures clean Debug and Release builds on Linux and macOS and runs the
complete CTest suite for each. A separate Linux job runs the same suite with both
sanitizers enabled.

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

## Match reconciliation review CLI

The review CLI operates on an existing versioned review store. Catalog authoring remains
explicit and programmatic: construct a `CanonicalIdentityCatalog`, pass it to
`MatchReviewStore`, and call `createMatchReviewStore` once to create the initial file.
The APIs are declared in `include/emberdb/reconciliation/match_review.h` and
`include/emberdb/persistence/match_review_file.h`. This keeps provider identity
decisions out of command-line heuristics.

Generate qualified candidates from StatsBomb and Wyscout match metadata:

```bash
./build/emberdb_cli reconcile generate \
  --review match-review.json \
  --left-provider statsbomb \
  --left-input statsbomb-matches.json \
  --right-provider wyscout \
  --right-input wyscout-matches.json
```

Generation uses the canonical team mappings in the review store, assigns stable numeric
candidate IDs, preserves previously generated records, and atomically saves the updated
store. Explicit competition and season mappings are also used when available. The
metadata providers currently supported by this command are `statsbomb` and `wyscout`.

List all candidates or filter by status:

```bash
./build/emberdb_cli reconcile list \
  --review match-review.json \
  --status unresolved
```

`--status` accepts `unresolved`, `accepted`, or `rejected`. Omitting it lists every
candidate.

Inspect confidence and competition, season, kickoff, home-team, away-team, and score
evidence:

```bash
./build/emberdb_cli reconcile inspect \
  --review match-review.json \
  --candidate-id 1
```

Accept against an existing canonical match:

```bash
./build/emberdb_cli reconcile accept \
  --review match-review.json \
  --candidate-id 1 \
  --canonical-match-id 100 \
  --actor "reviewer@example.com" \
  --source "provider match pages" \
  --reason "Teams, kickoff, and score agree"
```

Reject with an auditable reason:

```bash
./build/emberdb_cli reconcile reject \
  --review match-review.json \
  --candidate-id 1 \
  --actor "reviewer@example.com" \
  --source "provider match pages" \
  --reason "Provider correction identifies another fixture"
```

Acceptance and rejection rewrite the review store atomically. Identical repeated
commands succeed; conflicting finalized decisions fail without changing the file. Each
write checks the revision that was loaded while holding an exclusive sibling lock, so a
stale writer cannot overwrite a newer review.

## Persistent event file format

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

## Persistent match review format

Match review files are separate UTF-8 JSON documents identified by
`emberdb-match-review` and format version 2. They store canonical competition, season,
team, player, and match catalogs; all provider mappings; generated match candidates;
numeric confidence; complete per-field evidence; decision status and provenance;
catalog-change provenance; and a monotonic store revision. Version 1 review files load
as revision zero with empty competition/season catalogs and no synthesized mappings;
the next successful write upgrades them to version 2.

Loading rebuilds the canonical catalog through its validation APIs and rejects duplicate
records, dangling mappings, invalid candidate values, contradictory decision fields,
invalid audit records, or accepted candidates whose durable mappings do not agree.
Unsupported versions fail explicitly. Saving holds a sibling `.lock`, verifies the
expected revision, writes a sibling `.tmp`, and renames it only after the complete
document is written. A stale writer, lock, or temporary file causes the save to fail
without touching the current review file.

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

The import summary counts an event as having player data when either a provider player
ID or a player name is present. This includes Metrica events, whose anonymized player
labels are names without stable numeric IDs.

Pass and carry end locations are supported. Outcomes are extracted from common StatsBomb detail objects (`pass`, `shot`, `duel`, `dribble`, and `goalkeeper`) when present.

## Current limitations

- Event `.ember` files are uncompressed and are loaded fully into memory; there is no metadata
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
- Canonical catalog authoring and provider mapping edits are still programmatic. The
  reconciliation CLI expects an existing review store and deliberately does not infer
  team, player, competition, or season identity.
- Match candidate generation through the CLI currently supports StatsBomb and Wyscout
  metadata. There is no automatic candidate acceptance.
- Accepted match mappings do not yet reconcile or rewrite football events.
- Outcome extraction is intentionally limited to known common detail objects; the raw provider event is not retained.

## Long-term direction

The intended system evolves from provider adapters to normalized events, columnar persistence, a limited SQL parser and planner, execution operators, and terminal/CSV/JSON output. Additional providers should be added only through adapters, never by leaking their raw schemas into storage.

The recommended next milestone is an explicit catalog-authoring and entity-review
workflow for provider metadata: import canonical catalog inputs, manage team/player plus
competition/season mappings, and review those identity decisions with the same durable
audit discipline. Event reconciliation should remain deferred until those prerequisite
entities are durable and explainable. SQL is also deliberately deferred; when resumed,
it should translate into the existing typed operations rather than bypassing them.
