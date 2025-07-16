cmake_minimum_required(VERSION 3.13)
project(kraken_doa)

file(GLOB_RECURSE SRC "src/*.cpp")
if (NOT SRC)
    return()
endif()

include(${SDRPP_MODULE_CMAKE})

find_package(Eigen3 3.3 CONFIG QUIET)
if (TARGET Eigen3::Eigen)
    set(KRAKEN_DOA_EIGEN Eigen3::Eigen)
else()
    find_path(EIGEN3_INCLUDE_DIR NAMES Eigen/Dense PATHS
        /opt/homebrew/include/eigen3 /usr/local/include/eigen3 /usr/include/eigen3)
endif()

target_include_directories(kraken_doa PRIVATE "src/")
if (KRAKEN_DOA_EIGEN)
    target_link_libraries(kraken_doa PRIVATE ${KRAKEN_DOA_EIGEN})
elseif (EIGEN3_INCLUDE_DIR)
    target_include_directories(kraken_doa PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()

# Test targets, one file per component
include(tests/heimdall_tests.cmake OPTIONAL)
include(tests/dsp_tests.cmake OPTIONAL)
include(tests/pipeline_tests.cmake OPTIONAL)
