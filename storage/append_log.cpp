#include "ekv/append_log.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

namespace ekv {
namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_io(const std::string& what) {
  throw Error(ErrorCode::IoError, what);
}

}  // namespace

AppendLog::~AppendLog() { close(); }

AppendLog::AppendLog(AppendLog&& other) noexcept
    : open_(other.open_),
      path_(std::move(other.path_)),
      file_(std::move(other.file_)),
      write_pos_(other.write_pos_) {
  other.open_ = false;
  other.write_pos_ = 0;
}

AppendLog& AppendLog::operator=(AppendLog&& other) noexcept {
  if (this != &other) {
    close();
    open_ = other.open_;
    path_ = std::move(other.path_);
    file_ = std::move(other.file_);
    write_pos_ = other.write_pos_;
    other.open_ = false;
    other.write_pos_ = 0;
  }
  return *this;
}

void AppendLog::ensure_open() const {
  if (!open_) {
    throw Error(ErrorCode::NotOpen, "append log is not open");
  }
}

void AppendLog::open(const fs::path& path) {
  if (open_) {
    throw Error(ErrorCode::AlreadyOpen, "append log is already open");
  }
  if (path.empty()) {
    throw Error(ErrorCode::InvalidArgument, "log path must not be empty");
  }

  const bool exists = fs::exists(path);
  std::ios::openmode mode =
      std::ios::in | std::ios::out | std::ios::binary;
  if (!exists) {
    mode |= std::ios::trunc;
  }

  file_.open(path, mode);
  if (!file_) {
    // Create empty file then reopen read/write (portable for new files).
    if (!exists) {
      std::ofstream create(path, std::ios::binary | std::ios::trunc);
      if (!create) {
        throw_io("failed to create log file: " + path.string());
      }
      create.close();
      file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_) {
      throw_io("failed to open log file: " + path.string());
    }
  }

  path_ = path;

  if (!exists || fs::file_size(path) == 0) {
    char header[kFileHeaderSize];
    encode_file_header(header);
    file_.seekp(0, std::ios::beg);
    file_.write(header, static_cast<std::streamsize>(kFileHeaderSize));
    if (!file_) {
      file_.close();
      throw_io("failed to write log file header");
    }
    file_.flush();
    write_pos_ = kFileHeaderSize;
  } else {
    char header[kFileHeaderSize];
    file_.seekg(0, std::ios::beg);
    file_.read(header, static_cast<std::streamsize>(kFileHeaderSize));
    if (file_.gcount() != static_cast<std::streamsize>(kFileHeaderSize)) {
      file_.close();
      throw_io("log file too small for header");
    }
    std::uint32_t version = 0;
    if (!file_header_valid(header, &version)) {
      file_.close();
      throw_io("invalid log file magic or unsupported version");
    }

    file_.clear();
    open_ = true;  // find_next_append_offset requires open_
    try {
      write_pos_ = find_next_append_offset();
      // Drop a torn tail so new appends are reachable on the next replay.
      // Resize with the stream closed — portable across libstdc++ / MSVC.
      file_.close();
      fs::resize_file(path, static_cast<std::uintmax_t>(write_pos_));
      file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
      if (!file_) {
        throw_io("failed to reopen log after tail trim");
      }
    } catch (...) {
      open_ = false;
      if (file_.is_open()) {
        file_.close();
      }
      path_.clear();
      write_pos_ = 0;
      throw;
    }
    return;
  }

  file_.clear();
  open_ = true;
}

std::uint64_t AppendLog::find_next_append_offset() const {
  ensure_open();
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(kFileHeaderSize), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek while recovering append offset");
  }

  std::uint64_t pos = kFileHeaderSize;
  while (true) {
    char header[kRecordHeaderSize];
    file_.read(header, static_cast<std::streamsize>(kRecordHeaderSize));
    const auto got = file_.gcount();
    if (got == 0) {
      break;
    }
    if (got != static_cast<std::streamsize>(kRecordHeaderSize)) {
      break;  // torn header
    }

    const auto decoded = decode_record_header(header);
    if (!record_lengths_sane(decoded.type, decoded.key_len, decoded.value_len)) {
      throw_io("corrupt record header while recovering append offset");
    }

    const std::uint64_t payload =
        static_cast<std::uint64_t>(decoded.key_len) + decoded.value_len;
    const std::uint64_t next = pos + kRecordHeaderSize + payload;

    // Read/skip payload; short read => torn record.
    if (payload > 0) {
      std::vector<char> skip(static_cast<std::size_t>(payload));
      file_.read(skip.data(), static_cast<std::streamsize>(payload));
      if (static_cast<std::uint64_t>(file_.gcount()) != payload) {
        break;
      }
    }
    pos = next;
  }
  file_.clear();
  return pos;
}

void AppendLog::close() noexcept {
  if (!open_) {
    return;
  }
  try {
    file_.flush();
  } catch (...) {
    // noexcept close: best-effort flush only.
  }
  file_.close();
  path_.clear();
  write_pos_ = 0;
  open_ = false;
}

void AppendLog::sync() {
  ensure_open();
  file_.flush();
  if (!file_) {
    throw_io("failed to flush log");
  }
}

std::uint64_t AppendLog::append_record(std::uint8_t type, std::string_view key,
                                       std::string_view value) {
  ensure_open();
  if (key.empty()) {
    throw Error(ErrorCode::InvalidArgument, "key must not be empty");
  }
  if (key.size() > kMaxKeyLen) {
    throw Error(ErrorCode::InvalidArgument, "key exceeds maximum length");
  }
  if (value.size() > kMaxValueLen) {
    throw Error(ErrorCode::InvalidArgument, "value exceeds maximum length");
  }

  const auto key_len = static_cast<std::uint32_t>(key.size());
  const auto value_len = static_cast<std::uint32_t>(value.size());

  char header[kRecordHeaderSize];
  encode_record_header(header, type, key_len, value_len);

  file_.seekp(static_cast<std::streamoff>(write_pos_), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek log for append");
  }

  file_.write(header, static_cast<std::streamsize>(kRecordHeaderSize));
  file_.write(key.data(), static_cast<std::streamsize>(key.size()));
  if (!value.empty()) {
    file_.write(value.data(), static_cast<std::streamsize>(value.size()));
  }
  if (!file_) {
    throw_io("failed to append record to log");
  }
  file_.flush();
  if (!file_) {
    throw_io("failed to flush after append");
  }

  const std::uint64_t record_offset = write_pos_;
  write_pos_ += kRecordHeaderSize + key.size() + value.size();
  // Return absolute offset of the value payload (record + header + key).
  return record_offset + kRecordHeaderSize + key.size();
}

RecordLocator AppendLog::append_put(std::string_view key,
                                    std::string_view value) {
  RecordLocator loc;
  loc.value_offset = append_record(kRecordPut, key, value);
  loc.value_size = static_cast<std::uint32_t>(value.size());
  return loc;
}

void AppendLog::append_delete(std::string_view key) {
  (void)append_record(kRecordDelete, key, {});
}

std::string AppendLog::read_value(RecordLocator locator) const {
  ensure_open();
  if (locator.value_size == 0) {
    return {};
  }

  std::string out(locator.value_size, '\0');
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(locator.value_offset), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek log for read");
  }
  file_.read(out.data(), static_cast<std::streamsize>(locator.value_size));
  if (file_.gcount() != static_cast<std::streamsize>(locator.value_size)) {
    throw_io("short read while loading value from log");
  }
  return out;
}

void AppendLog::replay(const ReplayFn& on_record) const {
  ensure_open();
  if (!on_record) {
    return;
  }

  file_.clear();
  file_.seekg(static_cast<std::streamoff>(kFileHeaderSize), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek to first record");
  }

  std::uint64_t pos = kFileHeaderSize;
  std::vector<char> key_buf;

  while (true) {
    char header[kRecordHeaderSize];
    file_.read(header, static_cast<std::streamsize>(kRecordHeaderSize));
    const auto got = file_.gcount();
    if (got == 0) {
      break;  // clean EOF
    }
    if (got != static_cast<std::streamsize>(kRecordHeaderSize)) {
      break;  // truncated header — ignore tail
    }

    const auto decoded = decode_record_header(header);
    if (!record_lengths_sane(decoded.type, decoded.key_len, decoded.value_len)) {
      throw_io("corrupt record header during replay");
    }

    key_buf.resize(decoded.key_len);
    file_.read(key_buf.data(), static_cast<std::streamsize>(decoded.key_len));
    if (file_.gcount() != static_cast<std::streamsize>(decoded.key_len)) {
      break;  // truncated key
    }

    const std::uint64_t value_offset =
        pos + kRecordHeaderSize + decoded.key_len;

    if (decoded.value_len > 0) {
      // Skip value bytes without loading them into the index path.
      std::vector<char> skip(decoded.value_len);
      file_.read(skip.data(), static_cast<std::streamsize>(decoded.value_len));
      if (file_.gcount() != static_cast<std::streamsize>(decoded.value_len)) {
        break;  // truncated value
      }
    }

    RecordLocator loc;
    loc.value_offset = value_offset;
    loc.value_size = decoded.value_len;

    const std::string_view key(key_buf.data(), key_buf.size());
    on_record(decoded.type, key, loc);

    pos = value_offset + decoded.value_len;
  }

  file_.clear();
}

}  // namespace ekv
