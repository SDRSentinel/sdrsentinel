cmake_minimum_required(VERSION 3.13)
project(plutosdr_source)

file(GLOB SRC "src/*.cpp")

if (NOT MSVC AND NOT ANDROID)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        if (NOT EXISTS "/Library/Frameworks/iio.framework" OR NOT EXISTS "/Library/Frameworks/ad9361.framework")
            message(STATUS "iio/ad9361 frameworks not found: skipping plutosdr_source module")
            return()
        endif ()
    else ()
        find_package(PkgConfig QUIET)
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(LIBIIO QUIET libiio)
            pkg_check_modules(LIBAD9361 QUIET libad9361)
        endif ()
        if (NOT LIBIIO_FOUND OR NOT LIBAD9361_FOUND)
            message(STATUS "libiio/libad9361 not found: skipping plutosdr_source module")
            return()
        endif ()
    endif ()
endif ()

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(plutosdr_source PRIVATE "C:/Program Files/PothosSDR/lib/")
    target_include_directories(plutosdr_source PRIVATE "C:/Program Files/PothosSDR/include/")

    target_link_libraries(plutosdr_source PRIVATE libiio)
    target_link_libraries(plutosdr_source PRIVATE libad9361)
elseif (ANDROID)
    target_include_directories(plutosdr_source PUBLIC
        /sdr-kit/${ANDROID_ABI}/include
    )

    target_link_libraries(plutosdr_source PUBLIC
        /sdr-kit/${ANDROID_ABI}/lib/libxml2.so
        /sdr-kit/${ANDROID_ABI}/lib/libiio.so
        /sdr-kit/${ANDROID_ABI}/lib/libad9361.so
    )
else ()
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_include_directories(plutosdr_source PRIVATE "/Library/Frameworks/iio.framework/Headers")
        target_link_libraries(plutosdr_source PRIVATE "/Library/Frameworks/iio.framework/iio")

        target_include_directories(plutosdr_source PRIVATE "/Library/Frameworks/ad9361.framework/Headers")
        target_link_libraries(plutosdr_source PRIVATE "/Library/Frameworks/ad9361.framework/ad9361")
    else ()
        target_include_directories(plutosdr_source PRIVATE ${LIBIIO_INCLUDE_DIRS})
        target_link_directories(plutosdr_source PRIVATE ${LIBIIO_LIBRARY_DIRS})
        target_link_libraries(plutosdr_source PRIVATE ${LIBIIO_LIBRARIES})

        target_include_directories(plutosdr_source PRIVATE ${LIBAD9361_INCLUDE_DIRS})
        target_link_directories(plutosdr_source PRIVATE ${LIBAD9361_LIBRARY_DIRS})
        target_link_libraries(plutosdr_source PRIVATE ${LIBAD9361_LIBRARIES})
    endif ()
endif ()