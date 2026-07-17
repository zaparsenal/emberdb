# EmberDB

EmberDB is a local columnar analytics engine specialized for football event data. It is an educational but serious C++ systems project for learning ingestion, schema normalization, column-oriented storage, and query execution without trying to replace a general-purpose analytical database.

## What works today

The first two milestones implement:

- a provider-independent, typed `FootballEvent` model;
- a provider adapter interface and StatsBomb Open Data JSON adapter;
- safe preservation of missing possession, team, player, outcome, and coordinate values;
- a typed 18-column in-memory `FootballEventTable` with row reconstruction and consistency validation;
- provider-neutral typed equality filters and projections with explicit null results;
- deterministic query execution that preserves imported event order;
- an `import` CLI with deterministic summary counts and optional preview;
- a `query` CLI that translates column filters and projections into the programmatic query API; and
- offline unit fixtures covering ingestion failures, coordinates, nulls, table behavior, and queries.

SQL, persistent files, compression, analytical functions, and additional providers are not implemented.

## Architecture

```text
StatsBomb event JSON
        |
        v
EventProviderAdapter / StatsBombEventAdapter
        |
        v
provider-independent FootballEvent values
        |
        v
in-memory FootballEventTable (one typed vector per field)
        |
        v
typed equality filters and projections
        |
        v
CLI tabular results
```

Raw StatsBomb JSON is confined to the adapter. Storage accepts only normalized events, leaving a clean seam for future provider adapters.

The current 18 logical columns are provider event ID, match ID, period, timestamp, minute, second, possession ID, team ID/name, player ID/name, event type, outcome, start x/y, end x/y, and provider.

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
Columns: 18
Events with player data: 2
Events with start locations: 2
Events with end locations: 2

Preview
0: id=evt-pass-1 type=Pass team=Ember FC player=Alex Forward start=(42.500000, 31.250000) end=(71.000000, 22.500000)
1: id=evt-carry-2 type=Carry team=Ember FC player=Alex Forward start=(71.000000, 22.500000) end=(78.000000, 29.000000)
```

`--match-id` is required because StatsBomb event files do not reliably include match context in each event. Provider names are selected case-sensitively; currently only `statsbomb` is accepted.

Filter and project events:

```bash
./build/emberdb_cli query \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --filter event_type=Pass \
  --project player_name,minute,start_x,start_y
```

Example output (columns are tab-separated):

```text
Matched 1 event
player_name    minute  start_x start_y
Alex Forward   12      42.5    31.25
```

`--filter` implements typed equality and may be repeated; repeated filters use `AND`
semantics. Values are parsed according to the selected column, so `minute=12` is an
integer comparison while `event_type=Pass` is a text comparison. Timestamp filter
values are milliseconds. Projected nulls print as `NULL`, result rows retain source
order, and duplicate projection columns are rejected.

The stable query column names are: `provider_event_id`, `match_id`, `period`,
`timestamp`, `minute`, `second`, `possession_id`, `team_id`, `team_name`, `player_id`,
`player_name`, `event_type`, `outcome`, `start_x`, `start_y`, `end_x`, `end_y`, and
`provider`.

The query API is declared in `include/emberdb/query/event_query.h`. It accepts typed
`EqualityPredicate` values rather than strings; the CLI is only a translation boundary.

## Data and coordinate semantics

StatsBomb timestamps are parsed into millisecond durations relative to the event period. The provider's `minute` and `second` fields are also retained as typed values.

Coordinates currently preserve StatsBomb's native pitch scale (normally 120 by 80). They are **not** yet converted to a common coordinate system. The provider field makes this interpretation explicit until a normalization policy is introduced.

Pass and carry end locations are supported. Outcomes are extracted from common StatsBomb detail objects (`pass`, `shot`, `duel`, `dribble`, and `goalkeeper`) when present.

## Current limitations

- Imports live only for the duration of the CLI process; there is no persistent file format.
- Equality is the only filter operation; there is no ordering, limiting, aggregation,
  SQL, optimizer, or general expression evaluation yet.
- No compression, dictionary encoding, parallelism, SIMD, or memory mapping is used.
- Only one event JSON file and one explicit match ID can be imported per invocation.
- Only StatsBomb is supported, and its coordinate scale is preserved rather than normalized.
- Outcome extraction is intentionally limited to known common detail objects; the raw provider event is not retained.

## Long-term direction

The intended system evolves from provider adapters to normalized events, columnar persistence, a limited SQL parser and planner, execution operators, and terminal/CSV/JSON output. Additional providers should be added only through adapters, never by leaking their raw schemas into storage.

The recommended next milestone is coordinate normalization into a documented common
pitch scale. Persistent columnar storage, aggregations and `GROUP BY`, a limited SQL
parser, a second provider adapter, and cross-provider reconciliation should follow in
that order. SQL should translate into the existing typed filter and projection
operations rather than bypassing them.
