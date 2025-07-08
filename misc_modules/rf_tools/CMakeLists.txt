cmake_minimum_required(VERSION 3.13)
project(rf_tools)

file(GLOB_RECURSE SRC "src/*.cpp")

# Embed the GTRI B210 power calibration table (src/pdw/b210_cal.json) so the default
# table does not depend on the working directory. The JSON is split into raw string
# chunks to stay under compiler string literal limits.
set(B210_CAL_JSON "${CMAKE_CURRENT_SOURCE_DIR}/src/pdw/b210_cal.json")
set(B210_CAL_CPP "${CMAKE_CURRENT_BINARY_DIR}/b210_cal_data.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${B210_CAL_JSON}")
file(READ "${B210_CAL_JSON}" B210_CAL_CONTENT)
string(LENGTH "${B210_CAL_CONTENT}" B210_CAL_LEN)
set(B210_CAL_OUT "namespace rf_tools {\nextern const char* const kB210CalChunks[];\nextern const unsigned kB210CalChunkCount;\nconst char* const kB210CalChunks[] = {\n")
set(B210_CAL_COUNT 0)
set(B210_CAL_POS 0)
while (B210_CAL_POS LESS B210_CAL_LEN)
    string(SUBSTRING "${B210_CAL_CONTENT}" ${B210_CAL_POS} 8000 B210_CAL_CHUNK)
    string(APPEND B210_CAL_OUT "R\"JSON(${B210_CAL_CHUNK})JSON\",\n")
    math(EXPR B210_CAL_POS "${B210_CAL_POS} + 8000")
    math(EXPR B210_CAL_COUNT "${B210_CAL_COUNT} + 1")
endwhile()
string(APPEND B210_CAL_OUT "};\nconst unsigned kB210CalChunkCount = ${B210_CAL_COUNT};\n}\n")
file(WRITE "${B210_CAL_CPP}.tmp" "${B210_CAL_OUT}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${B210_CAL_CPP}.tmp" "${B210_CAL_CPP}")
list(APPEND SRC "${B210_CAL_CPP}")

include(${SDRPP_MODULE_CMAKE})

target_include_directories(rf_tools PRIVATE "src/")

# Standalone root_dev Comprehensive Integration test executable
add_executable(test_root_dev_integration
    tests/test_root_dev_integration.cpp
    src/pdw/power_cal.cpp
    ${B210_CAL_CPP}
    src/export/pdw_binary_exporter.cpp
    src/mop/chirp_matched_filter.cpp
    src/vna/vna_analyzer.cpp
)
target_include_directories(test_root_dev_integration PRIVATE "src/")
target_compile_features(test_root_dev_integration PRIVATE cxx_std_17)
set_target_properties(test_root_dev_integration PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(test_root_dev_integration PRIVATE sdrpp_core)

# Standalone pulse detector test executable
add_executable(test_rf_tools_pulse_detector
    tests/test_pulse_detector.cpp
    src/pdw/pulse_detector.cpp
)
target_include_directories(test_rf_tools_pulse_detector PRIVATE "src/")
target_compile_features(test_rf_tools_pulse_detector PRIVATE cxx_std_17)
set_target_properties(test_rf_tools_pulse_detector PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(test_rf_tools_pulse_detector PRIVATE sdrpp_core)
