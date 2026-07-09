# Interview preparation notes

Short prompts grounded in **this** codebase. Prefer drawing the log layout on a
whiteboard, then walking put → get → crash → compact.

## Storage engine

1. Why append-only instead of update-in-place pages?
2. What does the in-memory index store after Phase 2, and why not the value?
3. Draw a put, an overwrite, and a delete on the same key. What is garbage?
4. How does replay reconstruct the latest value for each key?
5. Why little-endian explicit encoding instead of native `memcpy` of structs?

## Integrity & recovery

1. What does CRC-32 cover in a record? What does it not protect against?
2. Why is a bad CRC on the **last** record truncated, but mid-file CRC fails open?
3. Difference between `flush` and `fsync` / `FlushFileBuffers`?
4. What is a torn write? How do we discover one without CRC? With CRC?
5. Why bump the format version when adding CRC?

## Compaction

1. Why is rename (or `MoveFileEx` replace) the commit point?
2. Crash after writing `ekv.log.compact` but before replacing `ekv.log` — outcome?
3. Why drop tombstones during compaction?
4. How does `space_amp = log_bytes / live_bytes` drive auto-compact?
5. Tradeoffs of buffering all live values in RAM during compact?

## Concurrency

1. SWMR with `shared_mutex`: which methods take which lock?
2. Why is a shared `fstream` cursor unsafe for concurrent `get`?
3. Is concurrent `const` `unordered_map` access OK if no writer runs?
4. Can two threads `put` safely? What would you change for multi-writer?
5. How would you allow reads during compaction without a long exclusive lock?

## Ops & tradeoffs

1. When would you choose `SyncMode::Flush` vs `Full`?
2. Why still flush under `SyncMode::None`?
3. How would group commit reduce fsync cost without losing durability batching?
4. What breaks if two processes open the same data directory?
5. How would you extend this toward LSM / multi-level SSTables?

## Talking points from this project

- Educational Bitcask-style design, not RocksDB parity.
- Durability default is strong (`Full`); benchmarks can dial it down.
- Windows + Linux OS shims (`os_sync.cpp`) keep the core portable.
- Tests cover recovery, compaction, concurrency stress, and auto-compact.
