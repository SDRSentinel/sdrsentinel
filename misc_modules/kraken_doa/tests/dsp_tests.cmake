# DSP test: channelizer, array geometry and DOA estimator
add_executable(test_kraken_doa_dsp
    tests/test_dsp.cpp
    src/dsp/channelizer.cpp
    src/dsp/array_geometry.cpp
    src/dsp/doa_estimator.cpp
)
target_include_directories(test_kraken_doa_dsp PRIVATE "src/")
target_compile_features(test_kraken_doa_dsp PRIVATE cxx_std_17)
set_target_properties(test_kraken_doa_dsp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if (KRAKEN_DOA_EIGEN)
    target_link_libraries(test_kraken_doa_dsp PRIVATE ${KRAKEN_DOA_EIGEN})
elseif (EIGEN3_INCLUDE_DIR)
    target_include_directories(test_kraken_doa_dsp PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()
