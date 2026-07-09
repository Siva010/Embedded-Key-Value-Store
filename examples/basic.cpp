// Minimal usage sample for the Phase 2 durable store.
#include "ekv/store.hpp"

#include <filesystem>
#include <iostream>

int main() {
  const std::filesystem::path data_dir = "ekv_example_data";

  {
    ekv::Store db;
    db.open(data_dir);
    db.put("hello", "world");
    db.put("phase", "2-append-log");
    db.close();
  }

  // Reopen reads from ekv.log via index rebuild.
  ekv::Store db;
  db.open(data_dir);
  if (auto value = db.get("hello")) {
    std::cout << "hello => " << *value << '\n';
  }
  db.Delete("phase");
  std::cout << "entries: " << db.size() << '\n';
  db.close();
  return 0;
}
