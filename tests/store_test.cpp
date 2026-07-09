// Unit tests for Store + HashIndex + append log (Phase 2).
#include "ekv/append_log.hpp"
#include "ekv/hash_index.hpp"
#include "ekv/record.hpp"
#include "ekv/store.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

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
  const auto dir =
      fs::temp_directory_path() / (std::string("ekv_") + name);
  fs::remove_all(dir);
  return dir;
}

static void test_hash_index_locators() {
  ekv::HashIndex idx;
  EXPECT(idx.empty());

  ekv::RecordLocator a{100, 4};
  ekv::RecordLocator b{200, 8};
  idx.put("k1", a);
  idx.put("k2", b);
  EXPECT(idx.size() == 2);
  EXPECT(idx.get("k1")->value_offset == 100);
  EXPECT(idx.get("k1")->value_size == 4);
  EXPECT(!idx.get("missing").has_value());

  idx.put("k1", ekv::RecordLocator{300, 1});
  EXPECT(idx.get("k1")->value_offset == 300);

  EXPECT(idx.erase("k2"));
  EXPECT(!idx.erase("k2"));
  idx.clear();
  EXPECT(idx.empty());
}

static void test_record_header_roundtrip() {
  char buf[ekv::kRecordHeaderSize];
  ekv::encode_record_header(buf, ekv::kRecordPut, 3, 7);
  const auto h = ekv::decode_record_header(buf);
  EXPECT(h.type == ekv::kRecordPut);
  EXPECT(h.key_len == 3);
  EXPECT(h.value_len == 7);

  char fhdr[ekv::kFileHeaderSize];
  ekv::encode_file_header(fhdr);
  std::uint32_t ver = 0;
  EXPECT(ekv::file_header_valid(fhdr, &ver));
  EXPECT(ver == ekv::kLogFormatVersion);
}

static void test_append_log_put_read() {
  const fs::path dir = unique_temp_dir("log_put_read");
  fs::create_directories(dir);
  const fs::path log_path = dir / "t.log";

  ekv::RecordLocator loc;
  {
    ekv::AppendLog log;
    log.open(log_path);
    loc = log.append_put("alpha", "one");
    EXPECT(log.read_value(loc) == "one");
    const auto loc2 = log.append_put("beta", "two");
    EXPECT(log.read_value(loc2) == "two");
    log.close();
  }

  {
    ekv::AppendLog log;
    log.open(log_path);
    int puts = 0;
    log.replay([&](std::uint8_t type, std::string_view key,
                   ekv::RecordLocator l) {
      EXPECT(type == ekv::kRecordPut);
      if (key == "alpha") {
        EXPECT(log.read_value(l) == "one");
        ++puts;
      } else if (key == "beta") {
        EXPECT(log.read_value(l) == "two");
        ++puts;
      }
    });
    EXPECT(puts == 2);
    log.close();
  }

  fs::remove_all(dir);
}

static void test_store_crud() {
  const fs::path dir = unique_temp_dir("store_crud");

  ekv::Store db;
  db.open(dir);

  db.put("user:1", "alice");
  db.put("user:2", "bob");
  EXPECT(db.size() == 2);
  EXPECT(*db.get("user:1") == "alice");
  EXPECT(*db.get("user:2") == "bob");

  db.put("user:1", "alice-updated");
  EXPECT(*db.get("user:1") == "alice-updated");

  EXPECT(db.Delete("user:2"));
  EXPECT(!db.get("user:2").has_value());
  EXPECT(db.size() == 1);

  db.put("empty-val", "");
  EXPECT(db.get("empty-val").has_value());
  EXPECT(db.get("empty-val")->empty());

  db.close();
  fs::remove_all(dir);
}

static void test_store_lifecycle() {
  const fs::path dir = unique_temp_dir("store_life");

  ekv::Store db;
  EXPECT(!db.is_open());

  bool threw = false;
  try {
    db.open("");
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::InvalidArgument;
  }
  EXPECT(threw);

  db.open(dir);
  EXPECT(db.is_open());
  EXPECT(fs::is_directory(dir));
  EXPECT(fs::exists(dir / "ekv.log"));

  db.put("x", "1");
  db.close();
  EXPECT(!db.is_open());
  db.close();

  threw = false;
  try {
    db.put("x", "2");
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::NotOpen;
  }
  EXPECT(threw);

  db.open(dir);
  db.put("m", "moved");
  ekv::Store other = std::move(db);
  EXPECT(other.is_open());
  EXPECT(*other.get("m") == "moved");
  other.close();

  fs::remove_all(dir);
}

static void test_persistence_across_reopen() {
  const fs::path dir = unique_temp_dir("store_persist");

  {
    ekv::Store db;
    db.open(dir);
    db.put("a", "1");
    db.put("b", "2");
    db.put("a", "1b");  // overwrite
    db.Delete("b");
    db.put("c", "3");
    db.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(db.size() == 2);
    EXPECT(*db.get("a") == "1b");
    EXPECT(!db.get("b").has_value());
    EXPECT(*db.get("c") == "3");
    db.close();
  }

  fs::remove_all(dir);
}

static void test_delete_tombstone_survives_reopen() {
  const fs::path dir = unique_temp_dir("store_tombstone");

  {
    ekv::Store db;
    db.open(dir);
    db.put("gone", "x");
    EXPECT(db.Delete("gone"));
    db.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(db.empty());
    EXPECT(!db.get("gone").has_value());
    // Re-insert after tombstone.
    db.put("gone", "y");
    EXPECT(*db.get("gone") == "y");
    db.close();
  }

  {
    ekv::Store db;
    db.open(dir);
    EXPECT(*db.get("gone") == "y");
    db.close();
  }

  fs::remove_all(dir);
}

int main() {
  test_hash_index_locators();
  test_record_header_roundtrip();
  test_append_log_put_read();
  test_store_crud();
  test_store_lifecycle();
  test_persistence_across_reopen();
  test_delete_tombstone_survives_reopen();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "store_test: ok\n";
  return 0;
}
