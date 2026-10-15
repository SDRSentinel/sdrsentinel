cmake_minimum_required(VERSION 3.13)
project(usrp_source)

file(GLOB SRC "src/*.cpp")

if (NOT MSVC)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(LIBUHD QUIET uhd)
    endif ()

    if (NOT LIBUHD_FOUND)
        message(STATUS "uhd not found: skipping usrp_source module")
        return()
    endif ()
endif ()

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(usrp_source PRIVATE "C:/Program Files/PothosSDR/bin/")

    target_include_directories(usrp_source PRIVATE "C:/Program Files/PothosSDR/include/")

    target_link_libraries(usrp_source PRIVATE uhd)
else ()
    target_include_directories(usrp_source PRIVATE ${LIBUHD_INCLUDE_DIRS})
    target_link_directories(usrp_source PRIVATE ${LIBUHD_LIBRARY_DIRS})
    target_link_libraries(usrp_source PRIVATE ${LIBUHD_LIBRARIES})
endif ()