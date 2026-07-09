# Embedded Key-Value Store

A production-quality **embedded key-value store** built from scratch in modern
**C++20**. Educational focus: storage engine design, crash recovery, compaction,
and concurrency — not feature count.

Philosophy is intentionally close to a simplified LevelDB/RocksDB: append-only
persistence, in-memory indexing, and careful durability semantics.

## Status

| Phase | Scope                                      | Status  |
|-------|--------------------------------------------|---------|
| 0     | Project scaffold, CMake, layout            | Done    |
| 1     | Core API + in-memory hash index            | Done    |
| 2     | Append-only log + binary records           | Done    |
| 3     | CRC32, recovery, fsync, crash tests        | Planned |
| 4     | Compaction (write → fsync → atomic rename) | Planned |
| 5     | Single writer / multi-reader + stress      | Planned |
| 6     | Config, ARM/QEMU, benchmarks               | Planned |

**Phase 2 note:** Data is stored in `data/ekv.log` (append-only). The in-memory
index maps keys → file offsets. `open` replays the log. Old versions of keys
remain in the file until Phase 4 compaction. CRC32 / hard `fsync` policy land
in Phase 3.

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

## Quick start

```cpp
#include "ekv/store.hpp"

ekv::Store db;
db.open("data");          // creates data/ and data/ekv.log
db.put("key", "value");
if (auto v = db.get("key")) {
  // *v == "value"
}
db.Delete("key");
db.close();
// reopen later — put/delete history is replayed from ekv.log
```

## Layout

```text
include/ekv/   Public headers (store, hash_index, append_log, record, …)
store/         Store facade
storage/       Append-only log implementation
index/         Reserved for index .cpp in later phases
wal/           WAL details (optional split later)
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
