add_executable(ekv_bench bench_main.cpp)
target_link_libraries(ekv_bench PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_bench)
