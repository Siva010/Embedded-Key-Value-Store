# Tests are wired up as features land. This keeps `enable_testing()` valid
# from day one so CI and local workflows can run `ctest` immediately.

find_package(Threads REQUIRED)

add_executable(ekv_smoke smoke_test.cpp)
target_link_libraries(ekv_smoke PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_smoke)
add_test(NAME ekv_smoke COMMAND ekv_smoke)

add_executable(ekv_store_test store_test.cpp)
target_link_libraries(ekv_store_test PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_store_test)
add_test(NAME ekv_store_test COMMAND ekv_store_test)

add_executable(ekv_recovery_test recovery_test.cpp)
target_link_libraries(ekv_recovery_test PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_recovery_test)
add_test(NAME ekv_recovery_test COMMAND ekv_recovery_test)

add_executable(ekv_compaction_test compaction_test.cpp)
target_link_libraries(ekv_compaction_test PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_compaction_test)
add_test(NAME ekv_compaction_test COMMAND ekv_compaction_test)

add_executable(ekv_concurrency_test concurrency_test.cpp)
target_link_libraries(ekv_concurrency_test PRIVATE ekv::ekv Threads::Threads)
ekv_apply_warnings(ekv_concurrency_test)
add_test(NAME ekv_concurrency_test COMMAND ekv_concurrency_test)

add_executable(ekv_options_test options_test.cpp)
target_link_libraries(ekv_options_test PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_options_test)
add_test(NAME ekv_options_test COMMAND ekv_options_test)

add_executable(ekv_auto_compact_test auto_compact_test.cpp)
target_link_libraries(ekv_auto_compact_test PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_auto_compact_test)
add_test(NAME ekv_auto_compact_test COMMAND ekv_auto_compact_test)
