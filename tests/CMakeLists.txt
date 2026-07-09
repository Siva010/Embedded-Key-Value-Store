# Tests are wired up as features land. This keeps `enable_testing()` valid
# from day one so CI and local workflows can run `ctest` immediately.

# Smoke target: ensures the library links and C++20 is available.
add_executable(ekv_smoke smoke_test.cpp)
target_link_libraries(ekv_smoke PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_smoke)
add_test(NAME ekv_smoke COMMAND ekv_smoke)
