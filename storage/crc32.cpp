#include "ekv/crc32.hpp"

#include "ekv/record.hpp"

#include <array>

namespace ekv {
namespace {

// Generated once: crc32_table[i] = reflection of polynomial applied to byte i.
[[nodiscard]] const std::array<std::uint32_t, 256>& crc_table() noexcept {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        if ((c & 1u) != 0u) {
          c = 0xEDB88320u ^ (c >> 1);
        } else {
          c >>= 1;
        }
      }
      t[static_cast<std::size_t>(i)] = c;
    }
    return t;
  }();
  return table;
}

}  // namespace

std::uint32_t crc32_update(std::uint32_t crc, const void* data,
                           std::size_t len) noexcept {
  const auto* p = static_cast<const unsigned char*>(data);
  const auto& table = crc_table();
  for (std::size_t i = 0; i < len; ++i) {
    crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc;
}

std::uint32_t crc32(const void* data, std::size_t len) noexcept {
  return crc32_finish(crc32_update(kCrc32Init, data, len));
}

std::uint32_t crc32_record(std::uint8_t type, std::uint32_t key_len,
                           std::uint32_t value_len, std::string_view key,
                           std::string_view value) noexcept {
  char lens[8];
  write_u32_le(lens, key_len);
  write_u32_le(lens + 4, value_len);

  std::uint32_t c = kCrc32Init;
  c = crc32_update(c, &type, 1);
  c = crc32_update(c, lens, 8);
  if (!key.empty()) {
    c = crc32_update(c, key.data(), key.size());
  }
  if (!value.empty()) {
    c = crc32_update(c, value.data(), value.size());
  }
  return crc32_finish(c);
}

}  // namespace ekv
