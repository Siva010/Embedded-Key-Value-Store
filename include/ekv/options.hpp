#pragma once

#include <string_view>

namespace ekv {

// Durability policy applied after each successful append.
//
// All modes flush the userspace stream so same-process positioned reads
// (get) observe the latest append. They differ only in OS durable sync.
enum class SyncMode {
  // Flush only — no fsync. Fast; not power-fail safe. Good for benchmarks.
  None = 0,
  // Flush only (explicit name for the same behavior as None).
  Flush = 1,
  // Flush + OS durable sync (fsync / FlushFileBuffers). Default.
  Full = 2,
};

[[nodiscard]] constexpr std::string_view to_string(SyncMode mode) noexcept {
  switch (mode) {
    case SyncMode::None:
      return "none";
    case SyncMode::Flush:
      return "flush";
    case SyncMode::Full:
      return "full";
  }
  return "unknown";
}

// Options fixed at Store::open (Phase 6).
struct Options {
  // Per-append durability. Prefer Full for production-like runs.
  SyncMode sync_mode = SyncMode::Full;
};

}  // namespace ekv
