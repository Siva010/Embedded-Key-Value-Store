// Phase 4: compaction reclaims garbage; data survives rewrite + reopen.
#include "ekv/compaction.hpp"
#include "ekv/record.hpp"
#include "ekv/store.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

static void test_compact_reclaims_overwrites_and_deletes() {
  const fs::path dir = unique_temp_dir("compact_reclaim");

  ekv::Store db;
  db.open(dir);

  db.put("a", "1");
  db.put("b", "2");
  db.put("c", "3");
  // Lots of garbage.
  for (int i = 0; i < 50; ++i) {
    db.put("a", "x" + std::to_string(i));
  }
  db.put("a", "final");
  EXPECT(db.Delete("b"));
  db.put("d", "4");

  const auto before = db.log_size_bytes();
  EXPECT(before > 200);

  const auto stats = db.compact();
  EXPECT(stats.live_keys == 3);  // a, c, d
  EXPECT(stats.bytes_after < stats.bytes_before);
  EXPECT(db.log_size_bytes() == stats.bytes_after);
  EXPECT(db.size() == 3);
  EXPECT(*db.get("a") == "final");
  EXPECT(!db.get("b").has_value());
  EXPECT(*db.get("c") == "3");
  EXPECT(*db.get("d") == "4");

  db.close();

  // Durable after compact.
  db.open(dir);
  EXPECT(db.size() == 3);
  EXPECT(*db.get("a") == "final");
  EXPECT(!db.get("b").has_value());
  EXPECT(db.log_size_bytes() == stats.bytes_after);
  // No leftover compaction temps.
  EXPECT(!fs::exists(dir / ekv::kCompactTempName));
  EXPECT(!fs::exists(dir / ekv::kCompactOldName));
  db.close();

  fs::remove_all(dir);
}

static void test_compact_empty_and_idempotent() {
  const fs::path dir = unique_temp_dir("compact_empty");

  ekv::Store db;
  db.open(dir);
  auto s1 = db.compact();
  EXPECT(s1.live_keys == 0);
  EXPECT(s1.bytes_after == ekv::kFileHeaderSize);

  db.put("k", "v");
  auto s2 = db.compact();
  EXPECT(s2.live_keys == 1);
  EXPECT(*db.get("k") == "v");

  const auto size_after_first = db.log_size_bytes();
  auto s3 = db.compact();
  EXPECT(s3.live_keys == 1);
  EXPECT(db.log_size_bytes() == size_after_first);
  EXPECT(*db.get("k") == "v");
  db.close();

  fs::remove_all(dir);
}

static void test_open_cleans_abandoned_compact_temp() {
  const fs::path dir = unique_temp_dir("compact_abandon");

  {
    ekv::Store db;
    db.open(dir);
    db.put("keep", "yes");
    db.close();
  }

  // Simulate a crashed compact: temp exists alongside live log.
  {
    std::ofstream junk(dir / ekv::kCompactTempName, std::ios::binary);
    junk << "not a real compact file";
  }
  EXPECT(fs::exists(dir / ekv::kCompactTempName));

  ekv::Store db;
  db.open(dir);
  EXPECT(*db.get("keep") == "yes");
  EXPECT(!fs::exists(dir / ekv::kCompactTempName));
  db.close();

  fs::remove_all(dir);
}

static void test_open_promotes_compact_if_log_missing() {
  const fs::path dir = unique_temp_dir("compact_promote");

  {
    ekv::Store db;
    db.open(dir);
    db.put("solo", "value");
    // Produce a valid compact-shaped file by compacting then renaming.
    db.compact();
    db.close();
  }

  const auto logp = dir / "ekv.log";
  const auto compactp = dir / ekv::kCompactTempName;
  fs::rename(logp, compactp);
  EXPECT(!fs::exists(logp));
  EXPECT(fs::exists(compactp));

  ekv::Store db;
  db.open(dir);
  EXPECT(fs::exists(logp));
  EXPECT(!fs::exists(compactp));
  EXPECT(*db.get("solo") == "value");
  db.close();

  fs::remove_all(dir);
}

int main() {
  test_compact_reclaims_overwrites_and_deletes();
  test_compact_empty_and_idempotent();
  test_open_cleans_abandoned_compact_temp();
  test_open_promotes_compact_if_log_missing();

  if (failures != 0) {
    std::cerr << failures << " expectation(s) failed\n";
    return 1;
  }
  std::cout << "compaction_test: ok\n";
  return 0;
}
