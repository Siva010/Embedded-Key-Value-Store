#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace ekv {

// ---------------------------------------------------------------------------
// On-disk layout (Phase 2)
//
// File header (16 bytes, little-endian multi-byte fields):
//   magic[4]  = 'E' 'K' 'V' 'L'
//   version   = uint32  (currently 1)
//   reserved  = uint64  (0)
//
// Record (variable length):
//   type       = uint8   (1 = Put, 2 = Delete)
//   key_len    = uint32
//   value_len  = uint32  (0 for Delete)
//   key        = key_len bytes
//   value      = value_len bytes
//
// Phase 3 will extend integrity (CRC32) without changing the logical
// put/delete model; a format version bump may accompany that.
// ---------------------------------------------------------------------------

inline constexpr char kLogMagic[4] = {'E', 'K', 'V', 'L'};
inline constexpr std::uint32_t kLogFormatVersion = 1;
inline constexpr std::size_t kFileHeaderSize = 16;

inline constexpr std::size_t kRecordHeaderSize =
    1 + 4 + 4;  // type + key_len + value_len

inline constexpr std::uint8_t kRecordPut = 1;
inline constexpr std::uint8_t kRecordDelete = 2;

// Reject absurd lengths from a corrupt/truncated file (Phase 3 tightens this).
inline constexpr std::uint32_t kMaxKeyLen = 16u * 1024u * 1024u;    // 16 MiB
inline constexpr std::uint32_t kMaxValueLen = 64u * 1024u * 1024u;  // 64 MiB

// Where a live value lives in the log (index payload).
struct RecordLocator {
  std::uint64_t value_offset = 0;  // absolute file offset of value bytes
  std::uint32_t value_size = 0;
};

inline void write_u32_le(char* dest, std::uint32_t value) noexcept {
  const auto u = static_cast<unsigned char*>(static_cast<void*>(dest));
  u[0] = static_cast<unsigned char>(value & 0xFFu);
  u[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
  u[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
  u[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

inline void write_u64_le(char* dest, std::uint64_t value) noexcept {
  write_u32_le(dest, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
  write_u32_le(dest + 4,
               static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFu));
}

[[nodiscard]] inline std::uint32_t read_u32_le(const char* src) noexcept {
  const auto* u = reinterpret_cast<const unsigned char*>(src);
  return static_cast<std::uint32_t>(u[0]) |
         (static_cast<std::uint32_t>(u[1]) << 8) |
         (static_cast<std::uint32_t>(u[2]) << 16) |
         (static_cast<std::uint32_t>(u[3]) << 24);
}

[[nodiscard]] inline std::uint64_t read_u64_le(const char* src) noexcept {
  return static_cast<std::uint64_t>(read_u32_le(src)) |
         (static_cast<std::uint64_t>(read_u32_le(src + 4)) << 32);
}

inline void encode_file_header(char out[kFileHeaderSize]) noexcept {
  std::memcpy(out, kLogMagic, 4);
  write_u32_le(out + 4, kLogFormatVersion);
  write_u64_le(out + 8, 0);
}

[[nodiscard]] inline bool file_header_valid(const char hdr[kFileHeaderSize],
                                            std::uint32_t* version_out) noexcept {
  if (std::memcmp(hdr, kLogMagic, 4) != 0) {
    return false;
  }
  const auto version = read_u32_le(hdr + 4);
  if (version_out != nullptr) {
    *version_out = version;
  }
  return version == kLogFormatVersion;
}

inline void encode_record_header(char out[kRecordHeaderSize], std::uint8_t type,
                                 std::uint32_t key_len,
                                 std::uint32_t value_len) noexcept {
  out[0] = static_cast<char>(type);
  write_u32_le(out + 1, key_len);
  write_u32_le(out + 5, value_len);
}

struct DecodedRecordHeader {
  std::uint8_t type = 0;
  std::uint32_t key_len = 0;
  std::uint32_t value_len = 0;
};

[[nodiscard]] inline DecodedRecordHeader decode_record_header(
    const char in[kRecordHeaderSize]) noexcept {
  DecodedRecordHeader h;
  h.type = static_cast<std::uint8_t>(static_cast<unsigned char>(in[0]));
  h.key_len = read_u32_le(in + 1);
  h.value_len = read_u32_le(in + 5);
  return h;
}

[[nodiscard]] inline bool record_lengths_sane(std::uint8_t type,
                                              std::uint32_t key_len,
                                              std::uint32_t value_len) noexcept {
  if (key_len == 0 || key_len > kMaxKeyLen || value_len > kMaxValueLen) {
    return false;
  }
  if (type == kRecordDelete && value_len != 0) {
    return false;
  }
  if (type != kRecordPut && type != kRecordDelete) {
    return false;
  }
  return true;
}

}  // namespace ekv
