# Architecture

This document grows phase by phase. Module boundaries stay stable; each
phase records decisions (problem → options → tradeoffs → choice).

## Design goals

- Production-quality educational embedded KV store (LevelDB/RocksDB philosophy)
- Modern C++20, CMake, Linux-first (WSL / MinGW acceptable for local work)
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

1. **Core API + in-memory hash index** — `open/close/put/get/Delete` — **done**
2. **Append-only persistence** — binary records; index holds file offsets — **done**
3. **Integrity & recovery** — CRC32, log replay, fsync policy, crash tests
4. **Compaction** — write-new-file → fsync → atomic rename
5. **Concurrency** — single writer, multiple readers; stress tests
6. **Ops** — configuration, ARM/QEMU, benchmarks

## Non-goals (for now)

- Network protocol / multi-process shared access
- SQL or secondary indexes
- Compression / encryption (may be revisited later)
- Full RocksDB feature parity

---

## Phase 1 — Core API + in-memory index

### Problem

Provide a clean embedded API (`open`, `close`, `put`, `get`, `Delete`) that
callers can program against before durable storage exists, without painting
the public interface into a corner for Phase 2+.

### Options considered

| Area | Option A | Option B | Option C |
|------|----------|----------|----------|
| Index storage | `std::map` (ordered) | `std::unordered_map` (hash) | Custom open-addressing table |
| Value location | Values live in the index | Index stores handles/offsets only | Hybrid (small inline / large external) |
| Errors | Error codes on every call | Exceptions for hard errors | `std::expected` (C++23) |
| Lifecycle | Free functions + global | `Store` class, explicit open/close | Open in constructor only |

### Tradeoffs

- **Ordered map**: better range scans later, worse average lookup constants and
  more pointer chasing. We have no range API yet.
- **Hash map**: O(1) average get/put, matches LevelDB’s memtable role for point
  lookups; iteration order is undefined (acceptable).
- **Values in index (Phase 1)**: simplest correctness model; **must** change in
  Phase 2 so the index points at log offsets instead of copying every value.
- **Exceptions for open/put misuse, optional for get miss**: keeps the happy
  path readable; “key not found” is not exceptional.
- **`Store` + explicit `open`**: mirrors LevelDB/RocksDB, allows reopen, and
  accepts a data directory path before files exist.

### Decision

1. Public facade: `ekv::Store` in `include/ekv/store.hpp`, implementation in
   `store/store.cpp`.
2. Index: `ekv::HashIndex` wrapping `std::unordered_map<std::string, std::string>`
   (`include/ekv/hash_index.hpp`). Phase 2 will change the mapped type to a
   durable location.
3. Errors: `ekv::Error` / `ekv::ErrorCode` for hard failures; `std::optional`
   for `get` misses.
4. API detail: method is named `Delete` (capital D) because `delete` is a
   C++ keyword.
5. `open(path)` creates the directory now so Phase 2 can drop log files there
   without an API change. **Data is not durable across process restart in
   Phase 1.**

### Public surface (Phase 1)

```text
ekv::Store::open(path) / close() / is_open()
ekv::Store::put(key, value)
ekv::Store::get(key) -> optional<string>
ekv::Store::Delete(key) -> bool
ekv::Store::size() / empty()
```

Keys must be non-empty. Empty values are allowed.

### Assumptions & technical debt (Phase 1 — superseded where noted)

- Single-threaded only (Phase 5 adds locking).
- Hash lookup constructs a temporary `std::string` key (no transparent hash yet).

### Interview prompts

- Why hash map before an ordered structure?
- How would you evolve the index from “value in RAM” to “offset on disk”
  without breaking callers?
- Exception vs optional for “not found” — when is each appropriate?
- What does `open(path)` buy you before any file format exists?

---

## Phase 2 — Append-only log + binary records

### Problem

Survive process restart: durable put/delete history, fast point lookups without
scanning the whole file on every `get`.

### Options considered

| Area | Option A | Option B | Option C |
|------|----------|----------|----------|
| Persistence model | Update-in-place pages | Append-only log (Bitcask) | Full B-Tree on disk |
| Index contents | Full value in RAM | Key → file offset | Separate disk index file |
| I/O API | POSIX `read`/`write`/`pwrite` | `std::fstream` | memory-map |
| Delete | Physical erase in file | Tombstone record | Separate delete list |
| Torn write | Ignore (Phase 2) | Truncate to last good record | CRC + careful recovery (Phase 3) |

### Tradeoffs

- **Update-in-place**: complex free space, hard crash consistency.
- **Append-only**: simple crash story (prefix of log is valid), needs compaction
  later; file grows with every overwrite/delete.
- **Offset index**: RAM holds keys + locators only; large values stay on disk.
  Rebuild index by sequential replay on `open` (acceptable until a durable
  index or manfiest is added).
- **`std::fstream`**: portable on Windows/MinGW and Linux; Phase 3 can add
  `fsync` via native handles if needed.
- **Tombstones**: deletes are durable and ordered; space reclaimed in Phase 4.

### Decision

1. Single log file: `<data_dir>/ekv.log`.
2. File header: magic `EKVL`, version `1`, reserved (16 bytes).
3. Records: `type(u8) | key_len(u32le) | value_len(u32le) | key | value`.
4. `HashIndex` stores `RecordLocator{value_offset, value_size}`.
5. `put` / `Delete` append then update memory index; `get` reads by offset.
6. `open` replays the log (last write wins; delete removes from index).
7. On open, trim a torn tail to the end of the last complete record so new
   appends remain visible to replay.

### Assumptions & technical debt

- Flush to OS buffers after each append (`fstream::flush`); **not** full
  `fsync` durability (Phase 3).
- No per-record CRC (Phase 3).
- No compaction — overwritten keys leave garbage (Phase 4).
- Single log file only (no multi-file generations yet).
- Replay is O(file size) on every open.

### Interview prompts

- Why append-only instead of update-in-place?
- How do tombstones interact with replay order?
- What is a torn write, and why truncate the tail on open?
- How would CRC32 change the recovery condition for the last record?

---

## Decision log

| Phase | Decision | Rationale |
|-------|----------|-----------|
| 1 | `unordered_map` + values in memory | Fast point lookups; minimal surface before persistence |
| 1 | Exceptions for misuse, optional for miss | Matches “not found is normal” lookup semantics |
| 1 | `Delete` not `delete` | C++ keyword constraint |
| 2 | Append-only `ekv.log` + offset index | Simple durability + Bitcask-style reads |
| 2 | Tombstone deletes | Durable delete without rewriting history |
| 2 | Trim incomplete tail on open | Keeps append cursor consistent before CRC |
