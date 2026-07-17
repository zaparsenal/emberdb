# EmberDB Agent Guide

## Project vision

EmberDB is a local C++20 columnar analytics engine specialized for football event data. It is an educational systems project with production-minded boundaries: heterogeneous provider data is normalized into a stable event model, stored in typed columns, and eventually queried through a deliberately limited interface.

The intended flow is:

`provider data -> provider adapter -> normalized events -> columnar storage -> query planning -> execution -> terminal/CSV/JSON`

## Architectural boundaries

- Provider-specific parsing belongs under `ingestion`; storage and future query code must never depend on raw StatsBomb JSON.
- `FootballEvent` is the provider-independent interchange model.
- Missing source values remain explicit optional values. Do not silently default or discard malformed values.
- Coordinates currently retain the source-provider scale. A future normalization layer should convert them to a documented common pitch coordinate system.
- Keep components small and owned through values or RAII. Avoid global mutable state.
- Do not add SQL, persistence, compression, multithreading, SIMD, memory mapping, new providers, web services, or cloud infrastructure until a milestone requires them.
- Add an abstraction only when it serves current behavior or an imminent extension point.

## Repository layout

- `include/emberdb/common`: provider-neutral domain types
- `include/emberdb/ingestion`, `src/ingestion`: adapter contracts and implementations
- `include/emberdb/storage`, `src/storage`: columnar storage
- `src/main.cpp`: command-line boundary
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

Run a fixture import with:

```bash
./build/emberdb_cli import --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --limit 2
```

Run a typed fixture query with:

```bash
./build/emberdb_cli query --provider statsbomb --match-id 12345 \
  --input tests/fixtures/complete_events.json --filter event_type=Pass \
  --project player_name,minute,start_x,start_y
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
  --project player_name,minute,start_x,start_y
```

## Documentation discipline

Keep `README.md`, this file, CLI help, tests, and CMake targets synchronized with implementation. Clearly label planned work as planned. When architecture, commands, schema semantics, coordinate conventions, or limitations change, update the relevant documentation in the same change.
