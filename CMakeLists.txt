cmake_minimum_required(VERSION 3.16)

project(EmbeddedKVStore
  VERSION 0.1.0
  DESCRIPTION "Production-quality embedded key-value store (educational)"
  LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global options
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EKV_BUILD_TESTS "Build unit and stress tests" ON)
option(EKV_BUILD_BENCH "Build benchmarks" ON)
option(EKV_BUILD_EXAMPLES "Build example programs" ON)
option(EKV_ENABLE_WARNINGS "Enable strict compiler warnings" ON)

# ---------------------------------------------------------------------------
# Warnings (treat as errors only when explicitly desired later)
# ---------------------------------------------------------------------------
function(ekv_apply_warnings target)
  if(NOT EKV_ENABLE_WARNINGS)
    return()
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    )
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Library target (sources added as modules land)
# ---------------------------------------------------------------------------
add_library(ekv STATIC)
add_library(ekv::ekv ALIAS ekv)

target_include_directories(ekv
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Placeholder so the empty STATIC library configures cleanly on all generators.
# Real translation units replace this as features are implemented.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ekv_placeholder.cpp"
  "// Auto-generated placeholder; replaced by real sources in later commits.\n"
  "namespace ekv { void placeholder() {}\n}\n"
)
target_sources(ekv PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/ekv_placeholder.cpp")

ekv_apply_warnings(ekv)

# ---------------------------------------------------------------------------
# Optional subdirectories (exist when scaffolded; empty until next tasks)
# ---------------------------------------------------------------------------
if(EKV_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(EKV_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
  add_subdirectory(examples)
endif()

if(EKV_BUILD_BENCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt")
  add_subdirectory(bench)
endif()

# ---------------------------------------------------------------------------
# Install rules (minimal; expanded later)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS ekv
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
