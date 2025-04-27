cmake_minimum_required(VERSION 3.13)
project(rf_tools)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(rf_tools PRIVATE "src/")

find_package(Eigen3 3.3 CONFIG QUIET)
if (TARGET Eigen3::Eigen)
    target_link_libraries(rf_tools PRIVATE Eigen3::Eigen)
else()
    find_path(EIGEN3_INCLUDE_DIR NAMES Eigen/Dense PATHS
        "${CMAKE_SOURCE_DIR}/../vcpkg/packages/eigen3_arm64-osx/include/eigen3"
        "${CMAKE_SOURCE_DIR}/build/vcpkg_installed/arm64-osx/include/eigen3"
        "/opt/homebrew/include/eigen3"
        "/usr/local/include/eigen3"
    )
    if (EIGEN3_INCLUDE_DIR)
        target_include_directories(rf_tools PRIVATE ${EIGEN3_INCLUDE_DIR})
    endif()
endif()

# Standalone DOA test and CRLB benchmark executable
add_executable(test_rf_tools_doa
    tests/test_doa.cpp
    src/doa/covariance_accumulator.cpp
    src/doa/music_engine.cpp
    src/doa/array_synthesizer.cpp
)
target_include_directories(test_rf_tools_doa PRIVATE "src/")
target_compile_features(test_rf_tools_doa PRIVATE cxx_std_17)
set_target_properties(test_rf_tools_doa PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if (TARGET Eigen3::Eigen)
    target_link_libraries(test_rf_tools_doa PRIVATE Eigen3::Eigen)
elseif (EIGEN3_INCLUDE_DIR)
    target_include_directories(test_rf_tools_doa PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()
