# EmberDB Agent Guide

## Project vision

EmberDB is a local C++20 columnar analytics engine specialized for football event data. It is an educational systems project with production-minded boundaries: heterogeneous provider data is normalized into a stable event model, stored in typed columns, and eventually queried through a deliberately limited interface.

The intended flow is:

`provider data -> provider adapter -> normalized events -> columnar storage -> query planning -> execution -> terminal/CSV/JSON`

Provider metadata follows a separate path:

`provider metadata -> metadata adapter -> explicit canonical identity mappings -> canonical event identity`

## Architectural boundaries

- Provider-specific parsing belongs under `ingestion`; storage and future query code must never depend on raw StatsBomb JSON.
- `FootballEvent` is the provider-independent interchange model.
- Canonical match, team, and player identity stays separate from provider event fields;
  mappings must be explicit until a reconciliation milestone defines otherwise.
- Missing source values remain explicit optional values. Do not silently default or discard malformed values.
- Route normalized events through `validateFootballEvent`; adapters should add provider
  record context to validation failures rather than duplicating provider-neutral rules.
- Normalized coordinates use EmberDB's 0–100 by 0–100 pitch with attacks running left to right. Preserve provider coordinates in the `source_*` columns and validate bounds in each adapter.
- Keep components small and owned through values or RAII. Avoid global mutable state.
- Do not add SQL, persistence, compression, multithreading, SIMD, memory mapping, new providers, web services, or cloud infrastructure until a milestone requires them.
- Add an abstraction only when it serves current behavior or an imminent extension point.

## Repository layout

- `include/emberdb/common`: provider-neutral domain types
- `include/emberdb/identity`, `src/identity`: canonical entity catalogs and mappings
- `include/emberdb/ingestion`, `src/ingestion`: adapter contracts and implementations
- `include/emberdb/reconciliation`, `src/reconciliation`: explainable cross-provider
  match and provider-to-canonical entity candidate comparison without automatic
  identity mutation
- `include/emberdb/persistence`, `src/persistence`: versioned identity and reconciliation
  review persistence, separate from event-table storage
- `include/emberdb/storage`, `src/storage`: columnar storage
- `src/cli`: command parsing, execution orchestration, and terminal rendering
- `src/main.cpp`: minimal process entry point and top-level error boundary
- `tests`: offline unit tests and small fixtures
- `scripts`: future dataset generation, validation, and benchmarks
- `examples`, `data`: future examples and local data (do not commit large provider datasets)

## Coding standards

- Use C++20, standard-library types, RAII, and const-correct interfaces.
- Use typed numeric identifiers, `std::chrono` for time, and `std::optional` for nullable data.
- Compile cleanly with the warnings configured in `CMakeLists.txt`.
- Include useful file paths, event indexes, and field names in ingestion errors.
- Tests must be deterministic and require no network after dependencies are configured.
- Never fabricate benchmark results; record commands, data, hardware, and build mode.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the sanitizer suite with:

```bash
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DEMBERDB_ENABLE_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

Run a fixture import with:

```bash
./build/emberdb_cli import --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --limit 2
```

Run a typed fixture query with:

```bash
./build/emberdb_cli query --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --filter event_type=Pass \
  --project player_name,minute,start_x,start_y,source_start_x,source_start_y
```

Run a grouped fixture aggregation with:

```bash
./build/emberdb_cli query --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --group-by event_type \
  --aggregate 'count(*)' --aggregate 'avg(start_x)'
```

Persist and query a normalized fixture database with:

```bash
./build/emberdb_cli import --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --output match.ember
./build/emberdb_cli query --database match.ember --filter event_type=Pass \
  --project player_name,minute,start_x,start_y,source_start_x,source_start_y
```

Generate and review match reconciliation candidates from an existing review store with:

```bash
./build/emberdb_cli reconcile generate --review match-review.json \
  --left-provider statsbomb --left-input statsbomb-matches.json \
  --right-provider wyscout --right-input wyscout-matches.json
./build/emberdb_cli reconcile list --review match-review.json --status unresolved
./build/emberdb_cli reconcile inspect --review match-review.json --candidate-id 1
```

Initialize and explicitly author a canonical identity review store with:

```bash
./build/emberdb_cli catalog init --review match-review.json
./build/emberdb_cli catalog add --review match-review.json \
  --entity team --canonical-id 1 --name "Ember FC" \
  --actor "reviewer@example.com" --source "provider team page" \
  --reason "Create canonical team"
./build/emberdb_cli catalog map --review match-review.json \
  --entity team --canonical-id 1 --provider StatsBomb --provider-id 10 \
  --actor "reviewer@example.com" --source "StatsBomb teams" \
  --reason "Verified provider identity"
```

Generate and review provider-to-canonical entity candidates:

```bash
./build/emberdb_cli catalog candidates generate \
  --review match-review.json --entity player \
  --provider statsbomb --input statsbomb-lineups.json
./build/emberdb_cli catalog candidates list \
  --review match-review.json --entity player --status unresolved
./build/emberdb_cli catalog candidates inspect \
  --review match-review.json --candidate-id 1
./build/emberdb_cli catalog candidates accept \
  --review match-review.json --candidate-id 1 \
  --actor "reviewer@example.com" --source "provider profile" \
  --reason "Verified provider identity"
```

## Documentation discipline

Keep `README.md`, this file, CLI help, tests, and CMake targets synchronized with implementation. Clearly label planned work as planned. When architecture, commands, schema semantics, coordinate conventions, or limitations change, update the relevant documentation in the same change.
