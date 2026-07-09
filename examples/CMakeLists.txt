add_executable(ekv_basic basic.cpp)
target_link_libraries(ekv_basic PRIVATE ekv::ekv)
ekv_apply_warnings(ekv_basic)
