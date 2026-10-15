# Pipeline test: DoaPipeline against an in-process fake Heimdall server (test fixture only, not linked into the module)
if (WIN32)
    return()
endif()

add_executable(test_kraken_doa_pipeline
    tests/test_pipeline.cpp
    src/pipeline.cpp
    src/heimdall/iq_header.cpp
    src/heimdall/iq_client.cpp
    src/heimdall/control_client.cpp
    src/heimdall/tcp_util.cpp
    src/dsp/channelizer.cpp
    src/dsp/array_geometry.cpp
    src/dsp/doa_estimator.cpp
)
target_include_directories(test_kraken_doa_pipeline PRIVATE "src/")
target_compile_features(test_kraken_doa_pipeline PRIVATE cxx_std_17)
set_target_properties(test_kraken_doa_pipeline PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
target_link_libraries(test_kraken_doa_pipeline PRIVATE Threads::Threads)
if (KRAKEN_DOA_EIGEN)
    target_link_libraries(test_kraken_doa_pipeline PRIVATE ${KRAKEN_DOA_EIGEN})
elseif (EIGEN3_INCLUDE_DIR)
    target_include_directories(test_kraken_doa_pipeline PRIVATE ${EIGEN3_INCLUDE_DIR})
endif()
