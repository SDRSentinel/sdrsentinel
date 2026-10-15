cmake_minimum_required(VERSION 3.13)
project(soapy_source)

file(GLOB SRC "src/*.cpp")

if (NOT MSVC)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(SOAPY QUIET SoapySDR)
    endif ()

    if (NOT SOAPY_FOUND)
        message(STATUS "SoapySDR not found: skipping soapy_source module")
        return()
    endif ()
endif ()

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(soapy_source PRIVATE "C:/Program Files/PothosSDR/lib/")

    # Misc headers
    target_include_directories(soapy_source PRIVATE "C:/Program Files/PothosSDR/include/")

    target_link_libraries(soapy_source PRIVATE SoapySDR)
else ()
    target_include_directories(soapy_source PRIVATE ${SOAPY_INCLUDE_DIRS})
    target_link_directories(soapy_source PRIVATE ${SOAPY_LIBRARY_DIRS})
    target_link_libraries(soapy_source PRIVATE ${SOAPY_LIBRARIES})
endif ()