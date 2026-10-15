# Heimdall protocol tests (no SDR++ core dependency; the client test runs against an in-process fake server)
set(KRAKEN_HEIMDALL_SRC
    src/heimdall/iq_header.cpp
    src/heimdall/iq_client.cpp
    src/heimdall/control_client.cpp
    src/heimdall/tcp_util.cpp
)

add_executable(test_kraken_heimdall_header
    tests/test_heimdall_header.cpp
    src/heimdall/iq_header.cpp
)
target_include_directories(test_kraken_heimdall_header PRIVATE "src/")
target_compile_features(test_kraken_heimdall_header PRIVATE cxx_std_17)
set_target_properties(test_kraken_heimdall_header PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

# The fake server uses POSIX sockets; keep the portable header test on Windows.
if (WIN32)
    return()
endif()

add_executable(test_kraken_heimdall_client
    tests/test_heimdall_client.cpp
    ${KRAKEN_HEIMDALL_SRC}
)
target_include_directories(test_kraken_heimdall_client PRIVATE "src/")
target_compile_features(test_kraken_heimdall_client PRIVATE cxx_std_17)
set_target_properties(test_kraken_heimdall_client PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
target_link_libraries(test_kraken_heimdall_client PRIVATE Threads::Threads)
