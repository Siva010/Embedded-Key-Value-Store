# Embedded Key-Value Store

A production-quality **embedded key-value store** built from scratch in modern
**C++20**. Educational focus: storage engine design, crash recovery, compaction,
and concurrency — not feature count.

Philosophy is intentionally close to a simplified LevelDB/RocksDB: append-only
persistence, in-memory indexing, and careful durability semantics.

## Status

| Phase | Scope                                      | Status      |
|-------|--------------------------------------------|-------------|
| 0     | Project scaffold, CMake, layout            | In progress |
| 1     | Core API + in-memory hash index            | Planned     |
| 2     | Append-only log + binary records           | Planned     |
| 3     | CRC32, recovery, fsync, crash tests        | Planned     |
| 4     | Compaction (write → fsync → atomic rename) | Planned     |
| 5     | Single writer / multi-reader + stress      | Planned     |
| 6     | Config, ARM/QEMU, benchmarks               | Planned     |

## Requirements

- CMake ≥ 3.16
- C++20 compiler (GCC 10+, Clang 12+, or MSVC 19.29+)
- Linux preferred (WSL2 is fine on Windows)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional flags:

| Option              | Default | Meaning                |
|---------------------|---------|------------------------|
| `EKV_BUILD_TESTS`   | ON      | Unit / smoke tests     |
| `EKV_BUILD_BENCH`   | ON      | Benchmarks (later)     |
| `EKV_BUILD_EXAMPLES`| ON      | Example programs       |
| `EKV_ENABLE_WARNINGS` | ON    | Strict warnings        |

## Layout

```text
include/ekv/   Public headers
store/         Store facade (Phase 1+)
index/         Hash index (Phase 1+)
storage/       Append-only log (Phase 2+)
wal/           WAL details (Phase 2–3)
recovery/      Replay & integrity (Phase 3+)
compaction/    Log compaction (Phase 4+)
tests/         Unit, crash, stress tests
bench/         Benchmarks
examples/      Usage samples
docs/          Architecture & design notes
```

## Development workflow

- One task at a time: implement → build → test → document → **local commit**
- Conventional Commits (`feat:`, `fix:`, `build:`, `test:`, `docs:`, …)
- Do **not** push unless explicitly requested

See `AI_Development_Workflow_Rules.md` and `Embedded_Key_Value_Store_AI_Prompt.md`
for the full process and product requirements.

## License

TBD.
