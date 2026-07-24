# FindFFmpegMin.cmake
#
# Locates a prebuilt *minimal static* FFmpeg tree used only for TS→MP4
# stream-copy remux (libavformat/libavcodec/libavutil[/libswresample]).
#
# Produces:
#   ffmpeg_min              INTERFACE library (includes + whole-archive static libs)
#   FFmpegMin::ffmpeg_min   ALIAS of ffmpeg_min
#
# Root resolution (first match wins):
#   1) -DFFMPEG_MIN_ROOT=...
#   2) $ENV{FFMPEG_MIN_ROOT}
#   3) ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min
#
# Expected layout (from scripts/build-ffmpeg-min.sh):
#   <root>/include/libavformat/avformat.h
#   <root>/lib/libavformat.a  (or MSVC-equivalent static lib)
#   <root>/lib/libavcodec.a
#   <root>/lib/libavutil.a
#   <root>/lib/libswresample.a   (optional)

function(_ffmpeg_min_resolve_lib out_var libname root)
    set(_candidates
        "${root}/lib/lib${libname}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${root}/lib/${libname}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "${root}/lib/lib${libname}.a"
        "${root}/lib/lib${libname}.lib"
        "${root}/lib/${libname}.a"
        "${root}/lib/${libname}.lib"
    )
    set(_found "")
    foreach(_c IN LISTS _candidates)
        if (EXISTS "${_c}")
            set(_found "${_c}")
            break()
        endif()
    endforeach()
    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

set(_FFMPEG_MIN_CANDIDATES "")
if (DEFINED FFMPEG_MIN_ROOT AND NOT "${FFMPEG_MIN_ROOT}" STREQUAL "")
    list(APPEND _FFMPEG_MIN_CANDIDATES "${FFMPEG_MIN_ROOT}")
endif()
if (DEFINED ENV{FFMPEG_MIN_ROOT} AND NOT "$ENV{FFMPEG_MIN_ROOT}" STREQUAL "")
    list(APPEND _FFMPEG_MIN_CANDIDATES "$ENV{FFMPEG_MIN_ROOT}")
endif()
list(APPEND _FFMPEG_MIN_CANDIDATES "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min")

set(FFMPEG_MIN_ROOT_FOUND "")
foreach(_candidate IN LISTS _FFMPEG_MIN_CANDIDATES)
    if (EXISTS "${_candidate}/include/libavformat/avformat.h"
        AND EXISTS "${_candidate}/include/libavcodec/avcodec.h"
        AND EXISTS "${_candidate}/include/libavutil/avutil.h")
        _ffmpeg_min_resolve_lib(_avformat avformat "${_candidate}")
        _ffmpeg_min_resolve_lib(_avcodec avcodec "${_candidate}")
        _ffmpeg_min_resolve_lib(_avutil avutil "${_candidate}")
        if (_avformat AND _avcodec AND _avutil)
            set(FFMPEG_MIN_ROOT_FOUND "${_candidate}")
            set(FFMPEG_MIN_LIB_AVFORMAT "${_avformat}")
            set(FFMPEG_MIN_LIB_AVCODEC "${_avcodec}")
            set(FFMPEG_MIN_LIB_AVUTIL "${_avutil}")
            _ffmpeg_min_resolve_lib(_swresample swresample "${_candidate}")
            set(FFMPEG_MIN_LIB_SWRESAMPLE "${_swresample}")
            break()
        endif()
    endif()
endforeach()

if (FFMPEG_MIN_ROOT_FOUND STREQUAL "")
    message(FATAL_ERROR
        "FFmpegMin: minimal static FFmpeg not found.\n"
        "Build it first:\n"
        "  ./scripts/build-ffmpeg-min.sh\n"
        "Or pass -DFFMPEG_MIN_ROOT=/path/to/ffmpeg-min\n"
        "Required: include/libav{format,codec,util}/*.h and matching static libs under lib/.")
endif()

set(FFMPEG_MIN_INCLUDE_DIR "${FFMPEG_MIN_ROOT_FOUND}/include")

# Primary consumer-facing target. Includes are attached here so any target that
# links ffmpeg_min compiles against the same headers used to build the libs.
add_library(ffmpeg_min INTERFACE)
add_library(FFmpegMin::ffmpeg_min ALIAS ffmpeg_min)

target_include_directories(ffmpeg_min INTERFACE "${FFMPEG_MIN_INCLUDE_DIR}")

# Codec/bsf registration tables live in archive members the linker may drop
# unless each static archive is loaded wholly. Use whole-archive style link
# options only (do not also plain-link the same .a — that duplicates symbols).
set(_FFMPEG_MIN_ARCHIVES
    "${FFMPEG_MIN_LIB_AVFORMAT}"
    "${FFMPEG_MIN_LIB_AVCODEC}"
)
if (FFMPEG_MIN_LIB_SWRESAMPLE)
    list(APPEND _FFMPEG_MIN_ARCHIVES "${FFMPEG_MIN_LIB_SWRESAMPLE}")
endif()
list(APPEND _FFMPEG_MIN_ARCHIVES "${FFMPEG_MIN_LIB_AVUTIL}")

if (MSVC)
    foreach(_archive IN LISTS _FFMPEG_MIN_ARCHIVES)
        target_link_options(ffmpeg_min INTERFACE "/WHOLEARCHIVE:${_archive}")
    endforeach()
elseif (APPLE)
    foreach(_archive IN LISTS _FFMPEG_MIN_ARCHIVES)
        target_link_options(ffmpeg_min INTERFACE "LINKER:-force_load,${_archive}")
    endforeach()
else()
    # ELF: one whole-archive group preserves classic libav order.
    target_link_libraries(ffmpeg_min INTERFACE
        -Wl,--whole-archive
        "${FFMPEG_MIN_LIB_AVFORMAT}"
        "${FFMPEG_MIN_LIB_AVCODEC}"
    )
    if (FFMPEG_MIN_LIB_SWRESAMPLE)
        target_link_libraries(ffmpeg_min INTERFACE "${FFMPEG_MIN_LIB_SWRESAMPLE}")
    endif()
    target_link_libraries(ffmpeg_min INTERFACE
        "${FFMPEG_MIN_LIB_AVUTIL}"
        -Wl,--no-whole-archive
    )
endif()

# System deps of a typical static libav build (file protocol, no network).
find_package(ZLIB)
if (ZLIB_FOUND)
    target_link_libraries(ffmpeg_min INTERFACE ZLIB::ZLIB)
endif()

find_library(FFMPEG_MIN_BZIP2_LIBRARY NAMES bz2 bzip2)
if (FFMPEG_MIN_BZIP2_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${FFMPEG_MIN_BZIP2_LIBRARY}")
endif()

find_library(FFMPEG_MIN_ICONV_LIBRARY NAMES iconv)
if (FFMPEG_MIN_ICONV_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${FFMPEG_MIN_ICONV_LIBRARY}")
endif()

if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(ffmpeg_min INTERFACE Threads::Threads m dl)
endif()

if (WIN32)
    target_link_libraries(ffmpeg_min INTERFACE bcrypt secur32 ws2_32)
endif()

message(STATUS "FFmpegMin: using ${FFMPEG_MIN_ROOT_FOUND}")
message(STATUS "FFmpegMin: include ${FFMPEG_MIN_INCLUDE_DIR}")
message(STATUS "FFmpegMin: avformat ${FFMPEG_MIN_LIB_AVFORMAT}")
message(STATUS "FFmpegMin: avcodec  ${FFMPEG_MIN_LIB_AVCODEC}")
message(STATUS "FFmpegMin: avutil   ${FFMPEG_MIN_LIB_AVUTIL}")
