#include "ekv/os_sync.hpp"

#include "ekv/error.hpp"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ekv {
namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_io(const std::string& what) {
  throw Error(ErrorCode::IoError, what);
}

}  // namespace

void sync_path(const fs::path& path) {
#if defined(_WIN32)
  const std::wstring w = path.wstring();
  HANDLE h = CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    throw_io("CreateFileW failed during sync: " + path.string());
  }
  const BOOL ok = FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) {
    throw_io("FlushFileBuffers failed: " + path.string());
  }
#else
  const int fd = ::open(path.c_str(), O_RDWR);
  if (fd < 0) {
    throw_io("open failed during sync: " + path.string());
  }
  const int rc = ::fsync(fd);
  ::close(fd);
  if (rc != 0) {
    throw_io("fsync failed: " + path.string());
  }
#endif
}

void sync_directory(const fs::path& dir) {
#if defined(_WIN32)
  // Directory fsync is not generally available / required the same way.
  (void)dir;
#else
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    // Non-fatal for Phase 3 single-file updates; surface as I/O if critical later.
    return;
  }
  (void)::fsync(fd);
  ::close(fd);
#endif
}

}  // namespace ekv
