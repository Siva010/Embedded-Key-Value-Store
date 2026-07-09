// Phase 3: CRC integrity, torn tails, mid-file corruption, durable reopen.
#include "ekv/append_log.hpp"
#include "ekv/crc32.hpp"
#include "ekv/record.hpp"
#include "ekv/store.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int failures = 0;

#define EXPECT(cond)                                                         \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "EXPECT failed: " << #cond << " (" << __FILE__ << ":"    \
                << __LINE__ << ")\n";                                        \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static fs::path unique_temp_dir(const char* name) {
  const auto dir = fs::temp_directory_path() / (std::string("ekv_") + name);
  fs::remove_all(dir);
  return dir;
}

static std::vector<char> read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>());
}

static void write_file(const fs::path& p, const std::vector<char>& bytes) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static void test_crc32_known_vector() {
  // "123456789" → 0xCBF43926 (common CRC-32 check value)
  const auto c = ekv::crc32("123456789");
  EXPECT(c == 0xCBF43926u);
}

static void test_crc32_record_stable() {
  const auto a = ekv::crc32_record(ekv::kRecordPut, 3, 3, "key", "val");
  const auto b = ekv::crc32_record(ekv::kRecordPut, 3, 3, "key", "val");
  EXPECT(a == b);
  const auto c = ekv::crc32_record(ekv::kRecordPut, 3, 3, "key", "vaL");
  EXPECT(a != c);
}

static void test_store_survives_reopen_with_sync() {
  const fs::path dir = unique_temp_dir("rec_persist");
  {
    ekv::Store db;
    db.open(dir);
    db.put("k", "v");
    db.put("k2", "v2");
    db.Delete("k2");
    db.close();
  }
  {
    ekv::Store db;
    db.open(dir);
    EXPECT(db.size() == 1);
    EXPECT(*db.get("k") == "v");
    EXPECT(!db.get("k2").has_value());
    db.close();
  }
  fs::remove_all(dir);
}

static void test_torn_tail_truncated() {
  const fs::path dir = unique_temp_dir("rec_torn");
  const fs::path log = dir / "ekv.log";

  {
    ekv::Store db;
    db.open(dir);
    db.put("keep", "yes");
    db.put("also", "here");
    db.close();
  }

  auto bytes = read_file(log);
  EXPECT(bytes.size() > ekv::kFileHeaderSize + 10);
  // Chop the last 7 bytes → incomplete final record.
  bytes.resize(bytes.size() - 7);
  write_file(log, bytes);

  {
    ekv::AppendLog alog;
    alog.open(log);
    EXPECT(alog.recovered_tail_bytes() >= 1);
    int count = 0;
    alog.replay([&](std::uint8_t, std::string_view key, ekv::RecordLocator) {
      ++count;
      EXPECT(key == "keep" || key == "also");
    });
    // At least the first put must survive; second may be lost if torn into it.
    EXPECT(count >= 1);
    alog.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(db.get("keep").has_value());
    EXPECT(*db.get("keep") == "yes");
    db.close();
  }

  fs::remove_all(dir);
}

static void test_bad_crc_on_last_record_truncated() {
  const fs::path dir = unique_temp_dir("rec_badcrc_tail");
  const fs::path log = dir / "ekv.log";

  {
    ekv::Store db;
    db.open(dir);
    db.put("a", "1");
    db.put("b", "2");
    db.close();
  }

  auto bytes = read_file(log);
  // Flip last CRC byte (last 4 bytes of file).
  EXPECT(bytes.size() >= ekv::kFileHeaderSize + ekv::kRecordCrcSize);
  bytes[bytes.size() - 1] =
      static_cast<char>(static_cast<unsigned char>(bytes[bytes.size() - 1]) ^ 0xFFu);
  write_file(log, bytes);

  {
    ekv::AppendLog alog;
    alog.open(log);
    EXPECT(alog.recovered_tail_bytes() > 0);
    bool saw_a = false;
    bool saw_b = false;
    alog.replay([&](std::uint8_t, std::string_view key, ekv::RecordLocator) {
      if (key == "a") {
        saw_a = true;
      }
      if (key == "b") {
        saw_b = true;
      }
    });
    EXPECT(saw_a);
    EXPECT(!saw_b);  // last record dropped
    alog.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(*db.get("a") == "1");
    EXPECT(!db.get("b").has_value());
    // New writes still work after recovery.
    db.put("c", "3");
    db.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(*db.get("c") == "3");
    db.close();
  }

  fs::remove_all(dir);
}

static void test_mid_file_crc_corruption_fails_open() {
  const fs::path dir = unique_temp_dir("rec_mid_corrupt");
  const fs::path log = dir / "ekv.log";

  {
    ekv::Store db;
    db.open(dir);
    db.put("first", "1");
    db.put("second", "2");
    db.put("third", "3");
    db.close();
  }

  auto bytes = read_file(log);
  // Corrupt a byte in the first record's key (after file + record headers).
  // Later records stay intact so the CRC failure is not at EOF.
  const std::size_t first_rec_start = ekv::kFileHeaderSize;
  const std::size_t flip = first_rec_start + ekv::kRecordHeaderSize;  // first key byte
  // first + second records must fit so corruption is mid-file.
  EXPECT(bytes.size() > first_rec_start + ekv::record_total_size(5, 1) + 10);
  EXPECT(flip < bytes.size());
  bytes[flip] =
      static_cast<char>(static_cast<unsigned char>(bytes[flip]) ^ 0xA5u);
  write_file(log, bytes);

  bool threw = false;
  try {
    ekv::Store db;
    db.open(dir);
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::Corruption;
  }
  EXPECT(threw);

  fs::remove_all(dir);
}

static void test_append_after_recovery() {
  const fs::path dir = unique_temp_dir("rec_append_after");
  const fs::path log = dir / "ekv.log";

  {
    ekv::Store db;
    db.open(dir);
    db.put("x", "1");
    db.close();
  }

  auto bytes = read_file(log);
  bytes.push_back('Z');  // torn extra byte
  write_file(log, bytes);

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(*db.get("x") == "1");
    db.put("y", "2");
    db.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(*db.get("x") == "1");
    EXPECT(*db.get("y") == "2");
    db.close();
  }

  fs::remove_all(dir);
}

int main() {
  test_crc32_known_vector();
  test_crc32_record_stable();
  test_store_survives_reopen_with_sync();
  test_torn_tail_truncated();
  test_bad_crc_on_last_record_truncated();
  test_mid_file_crc_corruption_fails_open();
  test_append_after_recovery();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "recovery_test: ok\n";
  return 0;
}
