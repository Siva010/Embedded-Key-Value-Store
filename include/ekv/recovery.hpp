#pragma once

// Recovery policy for the append-only log (Phase 3).
//
// Implementation lives in AppendLog::open / AppendLog::scan_records.
// This header documents the contract for reviewers and interview prep.
//
// ## Goals
// 1. Never apply a partially written or bit-flipped record to the index.
// 2. Prefer availability of the last known-good prefix over failing open when
//    the only damage is a torn tail (classic crash during append).
// 3. Fail loudly on mid-file corruption (disk error, bug, deliberate damage).
//
// ## Rules
// | Condition                                      | Action                          |
// |------------------------------------------------|---------------------------------|
// | Incomplete trailing bytes                      | Truncate to last good offset    |
// | Full last record, CRC mismatch                 | Truncate (treat as torn tail)   |
// | Full record, CRC mismatch, more bytes after it | throw ErrorCode::Corruption     |
// | Insane lengths that still claim room in file   | throw ErrorCode::Corruption     |
//
// ## Durability
// Each successful append flushes the stream and calls sync_path (fsync /
// FlushFileBuffers). This is the "sync on every write" policy — simple and
// strong; a later phase may add batching / group commit for throughput.
//
// ## What recovery does *not* do (yet)
// - Multi-file manifests / generations
// - Checksum of the file header
// - Automatic salvage of non-tail damage

namespace ekv {
// Intentionally empty — policy is enforced by AppendLog.
}  // namespace ekv
