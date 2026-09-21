# EmberDB

EmberDB is a local C++20 tool for exploring football event data. It converts provider files into one event format, stores them in typed columns, and lets you filter, select, and aggregate events from the command line.

It currently reads StatsBomb Open Data JSON, Metrica event CSV, and the open Wyscout research JSON export. You can also save normalized events as a `.ember` file and query that file later without parsing the provider data again.

## Try it with the included fixture

You need CMake 3.20+, a C++20 compiler, and network access for the first configuration to fetch build dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Preview two fixture events:

```sh
./build/emberdb_cli import \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --limit 2
```

Query passes from the same file:

```sh
./build/emberdb_cli query \
  --provider statsbomb \
  --match-id 12345 \
  --input tests/fixtures/complete_events.json \
  --filter event_type=Pass \
  --project player_name,minute,start_x,start_y
```

For grouped results, replace the filter and projection with `--group-by event_type --aggregate 'count(*)'`. The CLI prints command usage when called without arguments.

## What you can do

- Import one match at a time from a supported provider, then query events in their original order.
- Use equality filters, column selection, and `COUNT`, `SUM`, `AVG`, `MIN`, and `MAX`, with optional grouping.
- Save an import to a checksummed `.ember` file using `--output match.ember`, then query it using `--database match.ember`.
- Normalize locations to a 0–100 pitch while retaining source coordinates.
- Review explicit cross-provider identity mappings and match candidates in a separate review store. Decisions are recorded; event rows are not automatically rewritten.

## Scope and limits

EmberDB is a focused local analytics project, not a general SQL database. It does not support SQL, range filters, ordering, compression, streaming scans, or automatic identity matching. A `.ember` file is loaded into memory for querying. The CLI does not overwrite an existing output file.

The [example canonical manifest](examples/catalog-manifest.json) and `tests/fixtures` provide small inputs for exploring the catalog and reconciliation commands. The [earlier README revisions](https://github.com/zaparsenal/emberdb/commits/main/README.md) retain the detailed command reference and design history.
