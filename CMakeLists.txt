cmake_minimum_required(VERSION 3.20)

project(STIRWhirOverGR VERSION 0.1.0 LANGUAGES CXX)

option(SWGR_BUILD_TESTS "Build unit tests" ON)
option(SWGR_BUILD_BENCH "Build benchmark entrypoints" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(NTL_LIBRARY NAMES ntl REQUIRED)
find_library(GMP_LIBRARY NAMES gmp REQUIRED)
find_library(M_LIBRARY NAMES m)

set(GALOIS_RING_BACKEND_SOURCES
    third_party/GaloisRing/src/GaloisRing/HenselLift.cpp
    third_party/GaloisRing/src/GaloisRing/Inverse.cpp
    third_party/GaloisRing/src/GaloisRing/PrimitiveElement.cpp
    third_party/GaloisRing/src/GaloisRing/utils.cpp)

set(STIR_OVER_GR_SOURCES
    src/domain.cpp
    src/ldt.cpp
    src/parameters.cpp
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
    src/fri/prover.cpp
    src/fri/verifier.cpp
    src/fri/proof_size_estimator.cpp
    src/stir/common.cpp
    src/stir/parameters.cpp
    src/stir/prover.cpp
    src/stir/verifier.cpp
    src/stir/proof_size_estimator.cpp
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

target_link_libraries(stir_over_gr PUBLIC galoisring_backend)

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
  add_executable(test_fri tests/test_fri.cpp)
  target_link_libraries(test_fri PRIVATE stir_over_gr)
  target_include_directories(test_fri PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_executable(test_stir tests/test_stir.cpp)
  target_link_libraries(test_stir PRIVATE stir_over_gr)
  target_include_directories(test_stir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  add_test(NAME test_gr_basic COMMAND $<TARGET_FILE:test_gr_basic>)
  add_test(NAME test_domain COMMAND $<TARGET_FILE:test_domain>)
  add_test(NAME test_fft3 COMMAND $<TARGET_FILE:test_fft3>)
  add_test(NAME test_folding COMMAND $<TARGET_FILE:test_folding>)
  add_test(NAME test_fri COMMAND $<TARGET_FILE:test_fri>)
  add_test(NAME test_stir COMMAND $<TARGET_FILE:test_stir>)
endif()

if(SWGR_BUILD_BENCH)
  add_executable(bench_proof_size_estimate bench/bench_proof_size_estimate.cpp)
  target_link_libraries(bench_proof_size_estimate PRIVATE stir_over_gr)

  add_executable(bench_time bench/bench_time.cpp)
  target_link_libraries(bench_time PRIVATE stir_over_gr)
endif()
