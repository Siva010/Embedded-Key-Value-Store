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

void replace_file(const fs::path& from, const fs::path& to) {
#if defined(_WIN32)
  const std::wstring wfrom = from.wstring();
  const std::wstring wto = to.wstring();
  // MOVEFILE_REPLACE_EXISTING: clobber destination.
  // MOVEFILE_WRITE_THROUGH: do not return until the rename is on disk.
  if (!MoveFileExW(wfrom.c_str(), wto.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw_io("MoveFileExW replace failed: " + from.string() + " -> " +
             to.string());
  }
#else
  std::error_code ec;
  fs::rename(from, to, ec);
  if (ec) {
    throw_io("rename replace failed: " + from.string() + " -> " + to.string() +
             ": " + ec.message());
  }
#endif
  // Ensure the directory entry is durable where the platform supports it.
  if (const auto parent = to.parent_path(); !parent.empty()) {
    sync_directory(parent);
  }
}

std::string read_path_region(const fs::path& path, std::uint64_t offset,
                             std::uint32_t length) {
  if (length == 0) {
    return {};
  }
  std::string out(length, '\0');

#if defined(_WIN32)
  const std::wstring w = path.wstring();
  HANDLE h = CreateFileW(w.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    throw_io("CreateFileW failed during positioned read: " + path.string());
  }

  LARGE_INTEGER li;
  li.QuadPart = static_cast<LONGLONG>(offset);
  if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
    CloseHandle(h);
    throw_io("SetFilePointerEx failed during positioned read");
  }

  DWORD got = 0;
  const BOOL ok =
      ReadFile(h, out.data(), static_cast<DWORD>(length), &got, nullptr);
  CloseHandle(h);
  if (!ok || got != static_cast<DWORD>(length)) {
    throw_io("short ReadFile during positioned read: " + path.string());
  }
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    throw_io("open failed during positioned read: " + path.string());
  }
  const ssize_t got =
      ::pread(fd, out.data(), static_cast<std::size_t>(length),
              static_cast<off_t>(offset));
  ::close(fd);
  if (got < 0 || static_cast<std::uint32_t>(got) != length) {
    throw_io("short pread during positioned read: " + path.string());
  }
#endif
  return out;
}

}  // namespace ekv
