// Minimal usage sample for the Phase 1 in-memory store.
#include "ekv/store.hpp"

#include <filesystem>
#include <iostream>

int main() {
  const std::filesystem::path data_dir = "ekv_example_data";

  ekv::Store db;
  db.open(data_dir);

  db.put("hello", "world");
  db.put("phase", "1-memory-only");

  if (auto value = db.get("hello")) {
    std::cout << "hello => " << *value << '\n';
  }

  db.Delete("phase");
  std::cout << "entries: " << db.size() << '\n';

  db.close();
  std::cout << "note: Phase 1 does not persist across process restarts\n";
  return 0;
}
