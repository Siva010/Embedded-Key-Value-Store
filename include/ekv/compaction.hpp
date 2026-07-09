#pragma once

#include <cstdint>
#include <cstddef>

namespace ekv {

// Result of a successful Store::compact() call.
struct CompactStats {
  std::uint64_t bytes_before = 0;  // log size before rewrite
  std::uint64_t bytes_after = 0;   // log size after rewrite
  std::size_t live_keys = 0;       // keys rewritten (no tombstones)
};

// Compaction file names under the store data directory (Phase 4).
// Temporary live rewrite target; abandoned on open if ekv.log already exists.
inline constexpr const char* kCompactTempName = "ekv.log.compact";
// Optional backup during replace (cleaned up if left behind).
inline constexpr const char* kCompactOldName = "ekv.log.old";

}  // namespace ekv
