cmake_minimum_required(VERSION 3.13)
project(rf_tools)

file(GLOB_RECURSE SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(rf_tools PRIVATE "src/")
