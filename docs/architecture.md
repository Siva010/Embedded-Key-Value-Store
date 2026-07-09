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
3. **Integrity & recovery** — CRC32, log replay, fsync policy, crash tests — **done**
4. **Compaction** — write-new-file → fsync → atomic rename — **done**
5. **Concurrency** — single writer, multiple readers; stress tests — **done**
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
2. File header: magic `EKVL`, version field, reserved (16 bytes).
3. Records (Phase 2 base): `type | key_len | value_len | key | value`.
4. `HashIndex` stores `RecordLocator{value_offset, value_size}`.
5. `put` / `Delete` append then update memory index; `get` reads by offset.
6. `open` replays the log (last write wins; delete removes from index).
7. On open, trim a torn tail to the end of the last complete record.

### Assumptions & technical debt (Phase 2 — superseded by Phase 3 where noted)

- No compaction — overwritten keys leave garbage (Phase 4).
- Single log file only (no multi-file generations yet).
- Replay is O(file size) on every open.

### Interview prompts

- Why append-only instead of update-in-place?
- How do tombstones interact with replay order?
- What is a torn write, and why truncate the tail on open?
- How would CRC32 change the recovery condition for the last record?

---

## Phase 3 — CRC32, fsync, recovery

### Problem

Detect torn or bit-flipped records; push data to stable storage so a process
crash (and, as far as the OS allows, a power loss) does not silently lose a
“successful” put; define clear open-time recovery rules.

### Options considered

| Area | Option A | Option B | Option C |
|------|----------|----------|----------|
| Checksum | No checksum | Trailing CRC-32 per record | Full-file Merkle / xxHash |
| CRC placement | Leading (before payload) | Trailing (after payload) | Separate checksum file |
| Sync policy | `flush` only | Sync every append | Group commit / timed sync |
| Bad last CRC | Fail open | Truncate tail | Retry / salvage |
| Mid-file bad CRC | Truncate there | Fail open | Skip record |

### Tradeoffs

- **CRC-32**: cheap, universal, detects torn writes and many bit errors; not
  cryptographic.
- **Trailing CRC**: natural for append (write body, then checksum); length still
  trusted before reading payload — combined with max-size caps.
- **Sync every write**: strongest simple durability, hurts throughput (Phase 6
  benchmarks will show this; batching can come later).
- **Truncate bad tail vs fail**: matches LevelDB-style “prefix is valid”; mid-file
  damage is not something silent truncation should hide.

### Decision

1. **Format version 2** (breaking vs Phase 2 v1 files).
2. Record: `type | key_len | value_len | key | value | crc32_le` where CRC covers
   `type||key_len||value_len||key||value` (zlib/IEEE polynomial).
3. After each append: `fstream::flush` + `sync_path` (`fsync` / `FlushFileBuffers`).
4. Recovery (also documented in `include/ekv/recovery.hpp`):
   - incomplete trailing record → truncate
   - full last record with bad CRC → truncate
   - bad CRC with more file bytes after → `ErrorCode::Corruption`
5. Crash tests simulate torn tails and CRC flips without killing the process.

### Assumptions & technical debt

- Sync-on-every-write is slow; no group commit yet.
- Header itself is not checksummed.
- True power-loss tests need hardware/VM kill; we approximate with file surgery.
### Interview prompts

- Why is a bad CRC on the *last* record treated differently from mid-file?
- What does `fsync` guarantee that `fflush` does not?
- Why not use a cryptographic hash for records?
- How would group commit change the failure model?

---

## Phase 4 — Compaction

### Problem

Append-only history accumulates garbage: superseded puts and delete tombstones.
Disk grows without bound even if the live key set is small.

### Options considered

| Area | Option A | Option B | Option C |
|------|----------|----------|----------|
| When | Manual `compact()` only | Size/ratio thresholds | Background thread |
| How | In-place squeeze | Write new file → fsync → rename | Multi-level LSM |
| Live set source | Full log scan | In-memory index snapshot | Separate hint file |
| Atomicity | Copy over in place | OS rename replace | Dual files + manifest |

### Tradeoffs

- **Manual compact**: simple, predictable, no background races (Phase 5 adds
  concurrency later). Callers (or Phase 6 config) decide when to run.
- **New file + rename**: classic crash-safe install; old file remains valid until
  rename commits. Matches the project roadmap explicitly.
- **Index snapshot**: O(live keys) reads of current values; no need to scan dead
  history. Requires holding values in RAM during the rewrite (acceptable for
  educational scale).
- **LSM**: future complexity; out of scope.

### Decision

1. Public API: `Store::compact() -> CompactStats`.
2. Write live puts only to `ekv.log.compact` (same v2 record format + CRC/fsync).
3. Close live log → move aside `ekv.log` → `replace_file(compact, ekv.log)` →
   drop backup → reopen and rebuild index.
4. On `open`, cleanup:
   - both `ekv.log` and `.compact` → delete abandoned `.compact`
   - only `.compact` → promote to `ekv.log`
   - leftover `.old` → delete
5. Windows uses `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)`; POSIX uses `rename`.

### Assumptions & technical debt

- Compaction is single-threaded and blocks the store (no readers during compact).
- Live values are buffered in memory for the rewrite.
- No automatic scheduling / space amplification trigger yet.
- No multi-generation logs or leveled compaction.

### Interview prompts

- Why is rename considered the commit point of compaction?
- What happens if we crash after writing `.compact` but before replacing `ekv.log`?
- Why drop tombstones during compact?
- How would you compact without buffering all values in RAM?

---

## Phase 5 — Concurrency (single writer / multi-reader)

### Problem

Allow concurrent point lookups while mutations remain correctly serialized with
the append log and in-memory index — without multi-process sharing.

### Options considered

| Area | Option A | Option B | Option C |
|------|----------|----------|----------|
| Lock | Global `mutex` | `shared_mutex` (RW) | Lock-free index + log seqlock |
| Read I/O | Shared `fstream` seek | Positioned `pread`/ReadFile | mmap |
| Writers | Single logical writer | Sharded writers | Actor queue |

### Tradeoffs

- **Global mutex**: simple, no true multi-reader parallelism.
- **`shared_mutex`**: matches SWMR; compact/put take exclusive lock and block
  readers (acceptable; compact is rare).
- **Shared fstream for get**: data race on get/put cursor — **unsafe**.
- **Positioned reads**: each get reads by absolute offset; concurrent readers OK
  while the shared lock is held (no writer).
- **Lock-free**: high complexity for educational phase; deferred.

### Decision

1. `mutable std::shared_mutex` on `Store`.
2. Shared: `get`, `size`, `empty`, `log_size_bytes`, `is_open`, `path`.
3. Unique: `put`, `Delete`, `compact`, `open`, `close`.
4. `AppendLog::read_value` → `read_path_region` (OS positioned read).
5. Stress tests: multi-reader, SWMR puts, compact under readers, deletes.

### Assumptions & technical debt

- Not multi-process safe (one process opens the data dir).
- No reader-writer preference tuning / writer starvation mitigation beyond STL.
- Move of `Store` requires exclusive access (documented).
- Compact still buffers live values in RAM under the exclusive lock.

### Interview prompts

- Why is concurrent `const` access to `unordered_map` OK only without writers?
- Why can't multiple threads share one `fstream` for random reads?
- What does SWMR guarantee about the value returned by `get` during a `put`?
- How would you allow reads during compaction without a long exclusive lock?

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
| 3 | Format v2 + trailing CRC-32 | Detect torn/corrupt records |
| 3 | Sync every append | Strong educational durability default |
| 3 | Tail truncate vs mid-file fail | Availability for crash tails; safety for deep damage |
| 4 | Manual compact, new file + atomic replace | Crash-safe GC without LSM complexity |
| 4 | Live set from hash index | O(live) rewrite; tombstones dropped |
| 5 | `shared_mutex` SWMR | Multiple concurrent gets; exclusive mutations |
| 5 | Positioned `read_path_region` for values | Avoid shared fstream cursor races |
