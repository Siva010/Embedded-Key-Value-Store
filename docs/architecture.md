# Architecture (scaffold)

This document will grow phase by phase. The scaffold only fixes the
**module boundaries** so later work has a stable home.

## Design goals

- Production-quality educational embedded KV store (LevelDB/RocksDB philosophy)
- Modern C++20, CMake, Linux-first
- No external database libraries; prefer the STL
- RAII, exception safety, thread safety (phased in)
- Justified design decisions, reviewable commits

## Suggested module layout

| Directory     | Responsibility                                      | Phase |
|---------------|-----------------------------------------------------|-------|
| `include/ekv` | Public API headers                                  | 1+    |
| `store/`      | Orchestration: open/close, put/get/delete facade    | 1     |
| `index/`      | In-memory (then durable) key → location map         | 1     |
| `storage/`    | Append-only log, binary record layout, I/O          | 2     |
| `wal/`        | Write-ahead log specifics (if split from storage)   | 2–3   |
| `recovery/`   | Startup replay, CRC validation, tail handling       | 3     |
| `compaction/` | Rewrite live keys, fsync, atomic rename             | 4     |
| `tests/`      | Unit, persistence, crash, stress tests              | 1+    |
| `bench/`      | Throughput / latency benchmarks                     | 6     |
| `examples/`   | Small programs demonstrating the API                | 1+    |
| `docs/`       | Architecture, formats, recovery, interview notes    | all   |

## Phased roadmap

1. **Core API + in-memory hash index** — `open/close/put/get/delete`
2. **Append-only persistence** — binary records; index holds file offsets
3. **Integrity & recovery** — CRC32, log replay, fsync policy, crash tests
4. **Compaction** — write-new-file → fsync → atomic rename
5. **Concurrency** — single writer, multiple readers; stress tests
6. **Ops** — configuration, ARM/QEMU, benchmarks

## Non-goals (for now)

- Network protocol / multi-process shared access
- SQL or secondary indexes
- Compression / encryption (may be revisited later)
- Full RocksDB feature parity

## Decision log

Decisions will be recorded here as each task lands (problem → options → tradeoffs → choice).
