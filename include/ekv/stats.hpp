#pragma once

#include <cstdint>
#include <cstddef>

namespace ekv {

// Snapshot of store space usage (cheap; no I/O).
struct StoreStats {
  std::size_t live_keys = 0;
  std::uint64_t log_bytes = 0;   // on-disk logical log size
  std::uint64_t live_bytes = 0;  // approx. bytes of live put records
  // log_bytes / max(live_bytes, 1). High values mean compaction would help.
  double space_amp = 1.0;
};

}  // namespace ekv
