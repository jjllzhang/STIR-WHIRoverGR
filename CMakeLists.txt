cmake_minimum_required(VERSION 3.20)

project(STIRWhirOverGR VERSION 0.1.0 LANGUAGES C CXX ASM)

option(SWGR_BUILD_TESTS "Build unit tests" ON)
option(SWGR_BUILD_BENCH "Build benchmark entrypoints" ON)
option(SWGR_USE_OPENMP "Enable optional OpenMP acceleration" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(NTL_LIBRARY NAMES ntl REQUIRED)
find_library(GMP_LIBRARY NAMES gmp REQUIRED)
find_library(M_LIBRARY NAMES m)

set(SWGR_OPENMP_ENABLED OFF)
if(SWGR_USE_OPENMP)
  find_package(OpenMP QUIET COMPONENTS CXX)
  if(OpenMP_CXX_FOUND)
    set(SWGR_OPENMP_ENABLED ON)
  endif()
endif()

set(GALOIS_RING_BACKEND_SOURCES
    third_party/GaloisRing/src/GaloisRing/HenselLift.cpp
    third_party/GaloisRing/src/GaloisRing/Inverse.cpp
    third_party/GaloisRing/src/GaloisRing/PrimitiveElement.cpp
    third_party/GaloisRing/src/GaloisRing/utils.cpp)

set(STIR_OVER_GR_SOURCES
    src/domain.cpp
    src/ldt.cpp
    src/parameters.cpp
    src/soundness/configurator.cpp
    src/utils.cpp
    src/algebra/gr_context.cpp
    src/algebra/gr_serialization.cpp
    src/algebra/teichmuller.cpp
    src/algebra/subgroup.cpp
    src/crypto/hash.cpp
    src/crypto/fs/transcript.cpp
    src/crypto/merkle_tree/merkle_tree.cpp
    src/crypto/merkle_tree/proof_planner.cpp
    src/poly_utils/polynomial.cpp
    src/poly_utils/fft3.cpp
    src/poly_utils/interpolation.cpp
    src/poly_utils/folding.cpp
    src/poly_utils/quotient.cpp
    src/poly_utils/degree_correction.cpp
    src/poly_utils/bs08.cpp
    src/fri/common.cpp
    src/fri/parameters.cpp
    src/fri/soundness.cpp
    src/fri/prover.cpp
    src/fri/verifier.cpp
    src/stir/common.cpp
    src/stir/parameters.cpp
    src/stir/prover.cpp
    src/stir/verifier.cpp
    src/whir/common.cpp
    src/whir/parameters.cpp
    src/whir/prover.cpp
    src/whir/verifier.cpp)

add_library(galoisring_backend STATIC ${GALOIS_RING_BACKEND_SOURCES})
add_library(swgr::galoisring_backend ALIAS galoisring_backend)
add_library(swgr::galoisring ALIAS galoisring_backend)

target_include_directories(
    galoisring_backend
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/GaloisRing/include)

target_compile_features(galoisring_backend PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(galoisring_backend PRIVATE /W4)
else()
  target_compile_options(galoisring_backend PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(galoisring_backend PUBLIC ${NTL_LIBRARY} ${GMP_LIBRARY})

if(M_LIBRARY)
  target_link_libraries(galoisring_backend PUBLIC ${M_LIBRARY})
endif()

set(SWGR_BLAKE3_SOURCES
    third_party/blake3/blake3.c
    third_party/blake3/blake3_dispatch.c
    third_party/blake3/blake3_portable.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|amd64|AMD64)$" AND UNIX AND
   (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang"))
  list(APPEND SWGR_BLAKE3_SOURCES
       third_party/blake3/blake3_avx2_x86-64_unix.S
       third_party/blake3/blake3_avx512_x86-64_unix.S
       third_party/blake3/blake3_sse2_x86-64_unix.S
       third_party/blake3/blake3_sse41_x86-64_unix.S)
endif()

add_library(swgr_blake3 STATIC ${SWGR_BLAKE3_SOURCES})
add_library(swgr::blake3 ALIAS swgr_blake3)
target_include_directories(swgr_blake3 PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/third_party/blake3)
target_compile_features(swgr_blake3 PUBLIC c_std_99)

if(NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|amd64|AMD64)$" AND UNIX AND
        (CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")))
  target_compile_definitions(swgr_blake3 PRIVATE BLAKE3_USE_NEON=0
                                                 BLAKE3_NO_SSE2
                                                 BLAKE3_NO_SSE41
                                                 BLAKE3_NO_AVX2
                                                 BLAKE3_NO_AVX512)
endif()

add_library(stir_over_gr STATIC ${STIR_OVER_GR_SOURCES})
add_library(swgr::stir_over_gr ALIAS stir_over_gr)
add_library(swgr::swgr ALIAS stir_over_gr)

target_include_directories(
    stir_over_gr
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(stir_over_gr PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(stir_over_gr PRIVATE /W4)
else()
  target_compile_options(stir_over_gr PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(stir_over_gr PUBLIC galoisring_backend swgr_blake3)

if(SWGR_OPENMP_ENABLED)
  target_compile_definitions(stir_over_gr PUBLIC SWGR_HAS_OPENMP=1)
  target_link_libraries(stir_over_gr PUBLIC OpenMP::OpenMP_CXX)
endif()

if(SWGR_BUILD_TESTS)
  enable_testing()

  add_executable(test_gr_basic tests/test_gr_basic.cpp)
  target_link_libraries(test_gr_basic PRIVATE stir_over_gr)
  target_include_directories(test_gr_basic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_domain tests/test_domain.cpp)
  target_link_libraries(test_domain PRIVATE stir_over_gr)
  target_include_directories(test_domain PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_fft3 tests/test_fft3.cpp)
  target_link_libraries(test_fft3 PRIVATE stir_over_gr)
  target_include_directories(test_fft3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_folding tests/test_folding.cpp)
  target_link_libraries(test_folding PRIVATE stir_over_gr)
  target_include_directories(test_folding PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_crypto tests/test_crypto.cpp)
  target_link_libraries(test_crypto PRIVATE stir_over_gr)
  target_include_directories(test_crypto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_fri tests/test_fri.cpp)
  target_link_libraries(test_fri PRIVATE stir_over_gr)
  target_include_directories(test_fri PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_stir tests/test_stir.cpp)
  target_link_libraries(test_stir PRIVATE stir_over_gr)
  target_include_directories(test_stir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_soundness_configurator tests/test_soundness_configurator.cpp)
  target_link_libraries(test_soundness_configurator PRIVATE stir_over_gr)
  target_include_directories(test_soundness_configurator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  add_test(NAME test_gr_basic COMMAND $<TARGET_FILE:test_gr_basic>)
  add_test(NAME test_domain COMMAND $<TARGET_FILE:test_domain>)
  add_test(NAME test_fft3 COMMAND $<TARGET_FILE:test_fft3>)
  add_test(NAME test_folding COMMAND $<TARGET_FILE:test_folding>)
  add_test(NAME test_crypto COMMAND $<TARGET_FILE:test_crypto>)
  add_test(NAME test_fri COMMAND $<TARGET_FILE:test_fri>)
  add_test(NAME test_stir COMMAND $<TARGET_FILE:test_stir>)
  add_test(NAME test_soundness_configurator COMMAND $<TARGET_FILE:test_soundness_configurator>)
endif()

if(SWGR_BUILD_BENCH)
  add_executable(bench_time bench/bench_time.cpp)
  target_link_libraries(bench_time PRIVATE stir_over_gr)
endif()
