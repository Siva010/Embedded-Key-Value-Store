// Unit tests for Store + HashIndex (Phase 1, in-memory only).
#include "ekv/hash_index.hpp"
#include "ekv/store.hpp"

#include <cassert>
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

static void test_hash_index() {
  ekv::HashIndex idx;
  EXPECT(idx.empty());
  EXPECT(idx.size() == 0);

  idx.put("k1", "v1");
  idx.put("k2", "v2");
  EXPECT(idx.size() == 2);
  EXPECT(idx.contains("k1"));
  EXPECT(*idx.get("k1") == "v1");
  EXPECT(!idx.get("missing").has_value());

  idx.put("k1", "v1b");
  EXPECT(*idx.get("k1") == "v1b");

  EXPECT(idx.erase("k2"));
  EXPECT(!idx.erase("k2"));
  EXPECT(idx.size() == 1);

  idx.clear();
  EXPECT(idx.empty());
}

static void test_store_crud() {
  const fs::path dir = fs::temp_directory_path() / "ekv_store_test_crud";
  fs::remove_all(dir);

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

  // Empty value is allowed; empty key is not.
  db.put("empty-val", "");
  EXPECT(db.get("empty-val").has_value());
  EXPECT(db.get("empty-val")->empty());

  db.close();
  fs::remove_all(dir);
}

static void test_store_lifecycle() {
  const fs::path dir = fs::temp_directory_path() / "ekv_store_test_life";
  fs::remove_all(dir);

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

  db.put("x", "1");
  db.close();
  EXPECT(!db.is_open());
  db.close();  // idempotent

  threw = false;
  try {
    db.put("x", "2");
  } catch (const ekv::Error& e) {
    threw = e.code() == ekv::ErrorCode::NotOpen;
  }
  EXPECT(threw);

  // Move leaves source closed and transfers open state.
  db.open(dir);
  db.put("m", "moved");
  ekv::Store other = std::move(db);
  EXPECT(other.is_open());
  EXPECT(*other.get("m") == "moved");
  other.close();

  fs::remove_all(dir);
}

static void test_phase1_not_durable() {
  const fs::path dir = fs::temp_directory_path() / "ekv_store_test_mem";
  fs::remove_all(dir);

  {
    ekv::Store db;
    db.open(dir);
    db.put("persist?", "no");
  }

  ekv::Store db;
  db.open(dir);
  EXPECT(db.empty());
  EXPECT(!db.get("persist?").has_value());
  db.close();

  fs::remove_all(dir);
}

int main() {
  test_hash_index();
  test_store_crud();
  test_store_lifecycle();
  test_phase1_not_durable();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "store_test: ok\n";
  return 0;
}
