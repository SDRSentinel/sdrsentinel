cmake_minimum_required(VERSION 3.13)
project(audio_source)

file(GLOB SRC "src/*.cpp")

if (NOT MSVC)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(RTAUDIO QUIET rtaudio)
    endif ()

    if (NOT RTAUDIO_FOUND)
        find_package(RtAudio CONFIG QUIET)
        if (RtAudio_FOUND)
            set(RTAUDIO_LIBRARIES RtAudio::rtaudio)
            set(RTAUDIO_FOUND TRUE)
        endif ()
    endif ()

    if (NOT RTAUDIO_FOUND)
        message(STATUS "rtaudio not found: skipping audio_source module")
        return()
    endif ()
endif ()

include(${SDRPP_MODULE_CMAKE})

if (MSVC)
    # Lib path
    target_link_directories(audio_source PRIVATE "C:/Program Files (x86)/RtAudio/lib")

    # Misc headers
    target_include_directories(audio_source PRIVATE "C:/Program Files (x86)/RtAudio/include/rtaudio")

    target_link_libraries(audio_source PRIVATE rtaudio)
else ()
    target_include_directories(audio_source PRIVATE ${RTAUDIO_INCLUDE_DIRS})
    target_link_directories(audio_source PRIVATE ${RTAUDIO_LIBRARY_DIRS})
    target_link_libraries(audio_source PRIVATE ${RTAUDIO_LIBRARIES})
endif ()