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
