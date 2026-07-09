#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ekv {

// CRC-32 (ISO-HDLC / zlib / Ethernet polynomial 0xEDB88320).
// init = 0xFFFFFFFF, final XOR = 0xFFFFFFFF.

[[nodiscard]] std::uint32_t crc32(const void* data, std::size_t len) noexcept;

[[nodiscard]] inline std::uint32_t crc32(std::string_view bytes) noexcept {
  return crc32(bytes.data(), bytes.size());
}

[[nodiscard]] std::uint32_t crc32_update(std::uint32_t crc, const void* data,
                                         std::size_t len) noexcept;

[[nodiscard]] inline std::uint32_t crc32_finish(std::uint32_t crc) noexcept {
  return crc ^ 0xFFFFFFFFu;
}

inline constexpr std::uint32_t kCrc32Init = 0xFFFFFFFFu;

// CRC over the logical record body (everything except the trailing CRC word):
//   type || key_len_le || value_len_le || key || value
[[nodiscard]] std::uint32_t crc32_record(std::uint8_t type,
                                         std::uint32_t key_len,
                                         std::uint32_t value_len,
                                         std::string_view key,
                                         std::string_view value) noexcept;

}  // namespace ekv
