#include "ekv/append_log.hpp"

#include "ekv/crc32.hpp"
#include "ekv/os_sync.hpp"

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

[[noreturn]] void throw_corruption(const std::string& what) {
  throw Error(ErrorCode::Corruption, what);
}

}  // namespace

AppendLog::~AppendLog() { close(); }

AppendLog::AppendLog(AppendLog&& other) noexcept
    : open_(other.open_),
      path_(std::move(other.path_)),
      file_(std::move(other.file_)),
      write_pos_(other.write_pos_),
      recovered_tail_bytes_(other.recovered_tail_bytes_) {
  other.open_ = false;
  other.write_pos_ = 0;
  other.recovered_tail_bytes_ = 0;
}

AppendLog& AppendLog::operator=(AppendLog&& other) noexcept {
  if (this != &other) {
    close();
    open_ = other.open_;
    path_ = std::move(other.path_);
    file_ = std::move(other.file_);
    write_pos_ = other.write_pos_;
    recovered_tail_bytes_ = other.recovered_tail_bytes_;
    other.open_ = false;
    other.write_pos_ = 0;
    other.recovered_tail_bytes_ = 0;
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

  recovered_tail_bytes_ = 0;
  const bool exists = fs::exists(path);
  std::ios::openmode mode = std::ios::in | std::ios::out | std::ios::binary;
  if (!exists) {
    mode |= std::ios::trunc;
  }

  file_.open(path, mode);
  if (!file_) {
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
    if (!file_) {
      file_.close();
      throw_io("failed to flush log file header");
    }
    try {
      sync_path(path_);
    } catch (...) {
      file_.close();
      path_.clear();
      throw;
    }
    write_pos_ = kFileHeaderSize;
    file_.clear();
    open_ = true;
    return;
  }

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
    throw_io("invalid log file magic or unsupported version (need v2)");
  }

  const auto file_size = static_cast<std::uint64_t>(fs::file_size(path));
  file_.clear();
  open_ = true;
  try {
    std::uint64_t append_pos = kFileHeaderSize;
    scan_records(nullptr, &append_pos);
    write_pos_ = append_pos;
    if (write_pos_ < file_size) {
      recovered_tail_bytes_ = file_size - write_pos_;
      file_.close();
      fs::resize_file(path, static_cast<std::uintmax_t>(write_pos_));
      file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
      if (!file_) {
        throw_io("failed to reopen log after tail trim");
      }
      sync_path(path_);
      if (const auto parent = path.parent_path(); !parent.empty()) {
        sync_directory(parent);
      }
    }
  } catch (...) {
    open_ = false;
    if (file_.is_open()) {
      file_.close();
    }
    path_.clear();
    write_pos_ = 0;
    recovered_tail_bytes_ = 0;
    throw;
  }
}

void AppendLog::scan_records(const ReplayFn* on_record,
                             std::uint64_t* out_append_pos) const {
  ensure_open();
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(kFileHeaderSize), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek to first record");
  }

  const auto file_end = static_cast<std::uint64_t>([&] {
    file_.seekg(0, std::ios::end);
    const auto end = file_.tellg();
    if (end < 0) {
      throw_io("failed to size log during scan");
    }
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(kFileHeaderSize), std::ios::beg);
    return end;
  }());

  std::uint64_t pos = kFileHeaderSize;
  std::vector<char> key_buf;
  std::vector<char> value_buf;

  while (pos < file_end) {
    const std::uint64_t remaining = file_end - pos;
    if (remaining < kRecordHeaderSize + kRecordCrcSize) {
      // Not enough bytes for a minimal record — recoverable tail.
      break;
    }

    char header[kRecordHeaderSize];
    file_.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    file_.read(header, static_cast<std::streamsize>(kRecordHeaderSize));
    if (file_.gcount() != static_cast<std::streamsize>(kRecordHeaderSize)) {
      break;
    }

    const auto decoded = decode_record_header(header);
    if (!record_lengths_sane(decoded.type, decoded.key_len, decoded.value_len)) {
      // Could be a torn tail with garbage bytes, or mid-file damage.
      // If no complete record of any size can follow, treat as tail; else fail.
      if (remaining < record_total_size(1, 0)) {
        break;
      }
      throw_corruption("corrupt record header at offset " + std::to_string(pos));
    }

    const std::uint64_t total =
        record_total_size(decoded.key_len, decoded.value_len);
    if (pos + total > file_end) {
      // Claimed record extends past EOF — torn write.
      break;
    }

    key_buf.resize(decoded.key_len);
    file_.read(key_buf.data(), static_cast<std::streamsize>(decoded.key_len));
    if (file_.gcount() != static_cast<std::streamsize>(decoded.key_len)) {
      break;
    }

    value_buf.resize(decoded.value_len);
    if (decoded.value_len > 0) {
      file_.read(value_buf.data(),
                 static_cast<std::streamsize>(decoded.value_len));
      if (file_.gcount() != static_cast<std::streamsize>(decoded.value_len)) {
        break;
      }
    }

    char crc_bytes[kRecordCrcSize];
    file_.read(crc_bytes, static_cast<std::streamsize>(kRecordCrcSize));
    if (file_.gcount() != static_cast<std::streamsize>(kRecordCrcSize)) {
      break;
    }
    const std::uint32_t stored_crc = read_u32_le(crc_bytes);
    const std::string_view key_sv(key_buf.data(), key_buf.size());
    const std::string_view val_sv(value_buf.data(), value_buf.size());
    const std::uint32_t expect =
        crc32_record(decoded.type, decoded.key_len, decoded.value_len, key_sv,
                     val_sv);

    if (stored_crc != expect) {
      // Bad CRC at end-of-file region → torn/corrupt tail. Mid-file → hard fail.
      if (pos + total == file_end) {
        break;
      }
      throw_corruption("CRC mismatch at offset " + std::to_string(pos));
    }

    if (on_record != nullptr && *on_record) {
      RecordLocator loc;
      loc.value_offset = pos + kRecordHeaderSize + decoded.key_len;
      loc.value_size = decoded.value_len;
      (*on_record)(decoded.type, key_sv, loc);
    }

    pos += total;
  }

  if (out_append_pos != nullptr) {
    *out_append_pos = pos;
  }
  file_.clear();
}

void AppendLog::close() noexcept {
  if (!open_) {
    return;
  }
  try {
    file_.flush();
  } catch (...) {
  }
  file_.close();
  path_.clear();
  write_pos_ = 0;
  recovered_tail_bytes_ = 0;
  open_ = false;
}

void AppendLog::sync() {
  ensure_open();
  file_.flush();
  if (!file_) {
    throw_io("failed to flush log");
  }
  sync_path(path_);
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

  const std::uint32_t crc =
      crc32_record(type, key_len, value_len, key, value);
  char crc_bytes[kRecordCrcSize];
  write_u32_le(crc_bytes, crc);

  file_.seekp(static_cast<std::streamoff>(write_pos_), std::ios::beg);
  if (!file_) {
    throw_io("failed to seek log for append");
  }

  file_.write(header, static_cast<std::streamsize>(kRecordHeaderSize));
  file_.write(key.data(), static_cast<std::streamsize>(key.size()));
  if (!value.empty()) {
    file_.write(value.data(), static_cast<std::streamsize>(value.size()));
  }
  file_.write(crc_bytes, static_cast<std::streamsize>(kRecordCrcSize));
  if (!file_) {
    throw_io("failed to append record to log");
  }

  // Durability: userspace flush then OS stable-media sync.
  sync();

  const std::uint64_t record_offset = write_pos_;
  write_pos_ += record_total_size(key_len, value_len);
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
  // Positioned OS read — does not move the writer fstream cursor, so concurrent
  // readers under Store::shared_mutex are safe w.r.t. the append stream.
  return read_path_region(path_, locator.value_offset, locator.value_size);
}

void AppendLog::replay(const ReplayFn& on_record) const {
  ensure_open();
  if (!on_record) {
    return;
  }
  // File was trimmed on open; a clean scan should consume the whole file.
  std::uint64_t ignored = 0;
  scan_records(&on_record, &ignored);
}

}  // namespace ekv
