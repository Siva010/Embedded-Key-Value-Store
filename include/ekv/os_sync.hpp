#pragma once

#include <filesystem>

namespace ekv {

// Push file data to stable storage as far as the OS allows.
// Call after the write stream has been flushed so kernel buffers hold the
// latest bytes. Throws Error(IoError) on failure.
void sync_path(const std::filesystem::path& path);

// Best-effort directory fsync (Linux/macOS). No-op on platforms without it.
// Important after creating/renaming files in a data directory (Phase 4).
void sync_directory(const std::filesystem::path& dir);

}  // namespace ekv
