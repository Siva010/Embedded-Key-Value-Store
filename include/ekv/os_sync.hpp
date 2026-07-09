#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ekv {

// Push file data to stable storage as far as the OS allows.
// Call after the write stream has been flushed so kernel buffers hold the
// latest bytes. Throws Error(IoError) on failure.
void sync_path(const std::filesystem::path& path);

// Best-effort directory fsync (Linux/macOS). No-op on platforms without it.
// Important after creating/renaming files in a data directory (Phase 4).
void sync_directory(const std::filesystem::path& dir);

// Atomically (as far as the OS allows) replace `to` with `from`.
// On POSIX this is rename(2); on Windows uses MoveFileEx REPLACE_EXISTING.
// Both paths must be on the same filesystem. Throws Error(IoError) on failure.
void replace_file(const std::filesystem::path& from,
                  const std::filesystem::path& to);

// Positioned read that does not share a stream cursor (safe for concurrent
// readers under Store's shared_mutex). Throws Error(IoError) on failure.
[[nodiscard]] std::string read_path_region(const std::filesystem::path& path,
                                           std::uint64_t offset,
                                           std::uint32_t length);

}  // namespace ekv
