# Locate a prebuilt minimal static FFmpeg tree (TS->MP4 remux only).
#
# Expected layout (produced by scripts/build-ffmpeg-min-macos.sh):
#   <root>/include/libavformat/avformat.h
#   <root>/lib/libavformat.a
#   <root>/lib/libavcodec.a
#   <root>/lib/libavutil.a
#   <root>/lib/libswresample.a
#
# Set -DFFMPEG_MIN_ROOT=/path/to/ffmpeg-min to override autodetection.

set(_FFMPEG_MIN_CANDIDATES)
if (DEFINED FFMPEG_MIN_ROOT AND NOT "${FFMPEG_MIN_ROOT}" STREQUAL "")
    list(APPEND _FFMPEG_MIN_CANDIDATES "${FFMPEG_MIN_ROOT}")
endif()
list(APPEND _FFMPEG_MIN_CANDIDATES
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min"
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min/prefix"
)

set(FFMPEG_MIN_ROOT_FOUND "")
foreach(_candidate IN LISTS _FFMPEG_MIN_CANDIDATES)
    if (EXISTS "${_candidate}/include/libavformat/avformat.h"
        AND EXISTS "${_candidate}/lib/libavformat.a"
        AND EXISTS "${_candidate}/lib/libavcodec.a"
        AND EXISTS "${_candidate}/lib/libavutil.a")
        set(FFMPEG_MIN_ROOT_FOUND "${_candidate}")
        break()
    endif()
endforeach()

if (FFMPEG_MIN_ROOT_FOUND STREQUAL "")
    message(FATAL_ERROR
        "Minimal static FFmpeg not found.\n"
        "Build it first, e.g.:\n"
        "  ./scripts/build-ffmpeg-min-macos.sh\n"
        "Or pass -DFFMPEG_MIN_ROOT=/path/to/ffmpeg-min")
endif()

set(FFMPEG_MIN_INCLUDE_DIR "${FFMPEG_MIN_ROOT_FOUND}/include")
set(FFMPEG_MIN_LIBRARY_DIR "${FFMPEG_MIN_ROOT_FOUND}/lib")

add_library(ffmpeg_min INTERFACE)
target_include_directories(ffmpeg_min INTERFACE "${FFMPEG_MIN_INCLUDE_DIR}")

# macOS needs force_load so codec/bsf registration tables are not stripped.
if (APPLE)
    target_link_options(ffmpeg_min INTERFACE
        "LINKER:-force_load,${FFMPEG_MIN_LIBRARY_DIR}/libavformat.a"
        "LINKER:-force_load,${FFMPEG_MIN_LIBRARY_DIR}/libavcodec.a"
        "LINKER:-force_load,${FFMPEG_MIN_LIBRARY_DIR}/libswresample.a"
        "LINKER:-force_load,${FFMPEG_MIN_LIBRARY_DIR}/libavutil.a"
    )
else()
    target_link_libraries(ffmpeg_min INTERFACE
        "${FFMPEG_MIN_LIBRARY_DIR}/libavformat.a"
        "${FFMPEG_MIN_LIBRARY_DIR}/libavcodec.a"
        "${FFMPEG_MIN_LIBRARY_DIR}/libswresample.a"
        "${FFMPEG_MIN_LIBRARY_DIR}/libavutil.a"
    )
endif()

# System deps used by the minimal static build.
find_package(ZLIB REQUIRED)
find_library(BZIP2_LIBRARY bz2)
find_library(ICONV_LIBRARY iconv)

target_link_libraries(ffmpeg_min INTERFACE ZLIB::ZLIB)
if (BZIP2_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${BZIP2_LIBRARY}")
endif()
if (ICONV_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${ICONV_LIBRARY}")
endif()

if (WIN32)
    target_link_libraries(ffmpeg_min INTERFACE bcrypt secur32 ws2_32)
endif()

message(STATUS "Using minimal static FFmpeg: ${FFMPEG_MIN_ROOT_FOUND}")
