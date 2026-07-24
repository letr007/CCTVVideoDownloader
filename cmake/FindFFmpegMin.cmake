# Locate a prebuilt minimal static FFmpeg tree (TS->MP4 remux only).
#
# Expected layout (from scripts/build-ffmpeg-min*.sh|ps1):
#   <root>/include/libavformat/avformat.h
#   <root>/lib/libavformat.(a|lib)
#   <root>/lib/libavcodec.(a|lib)
#   <root>/lib/libavutil.(a|lib)
#   <root>/lib/libswresample.(a|lib)   # optional but preferred
#
# Override: -DFFMPEG_MIN_ROOT=/path/to/ffmpeg-min

function(_ffmpeg_min_find_lib out_var libname root)
    set(_candidates
        "${root}/lib/lib${libname}.a"
        "${root}/lib/${libname}.a"
        "${root}/lib/lib${libname}.lib"
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

set(_FFMPEG_MIN_CANDIDATES)
if (DEFINED FFMPEG_MIN_ROOT AND NOT "${FFMPEG_MIN_ROOT}" STREQUAL "")
    list(APPEND _FFMPEG_MIN_CANDIDATES "${FFMPEG_MIN_ROOT}")
endif()
if (DEFINED ENV{FFMPEG_MIN_ROOT} AND NOT "$ENV{FFMPEG_MIN_ROOT}" STREQUAL "")
    list(APPEND _FFMPEG_MIN_CANDIDATES "$ENV{FFMPEG_MIN_ROOT}")
endif()
list(APPEND _FFMPEG_MIN_CANDIDATES
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min"
    "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min/prefix"
)

set(FFMPEG_MIN_ROOT_FOUND "")
foreach(_candidate IN LISTS _FFMPEG_MIN_CANDIDATES)
    if (EXISTS "${_candidate}/include/libavformat/avformat.h")
        _ffmpeg_min_find_lib(_avformat avformat "${_candidate}")
        _ffmpeg_min_find_lib(_avcodec avcodec "${_candidate}")
        _ffmpeg_min_find_lib(_avutil avutil "${_candidate}")
        if (_avformat AND _avcodec AND _avutil)
            set(FFMPEG_MIN_ROOT_FOUND "${_candidate}")
            set(FFMPEG_MIN_LIB_AVFORMAT "${_avformat}")
            set(FFMPEG_MIN_LIB_AVCODEC "${_avcodec}")
            set(FFMPEG_MIN_LIB_AVUTIL "${_avutil}")
            _ffmpeg_min_find_lib(_swresample swresample "${_candidate}")
            set(FFMPEG_MIN_LIB_SWRESAMPLE "${_swresample}")
            break()
        endif()
    endif()
endforeach()

if (FFMPEG_MIN_ROOT_FOUND STREQUAL "")
    message(FATAL_ERROR
        "Minimal static FFmpeg not found.\n"
        "Build it first:\n"
        "  macOS/Linux: ./scripts/build-ffmpeg-min.sh\n"
        "  Windows:     powershell -File scripts/build-ffmpeg-min-windows.ps1\n"
        "Or pass -DFFMPEG_MIN_ROOT=/path/to/ffmpeg-min")
endif()

set(FFMPEG_MIN_INCLUDE_DIR "${FFMPEG_MIN_ROOT_FOUND}/include")
set(FFMPEG_MIN_LIBRARY_DIR "${FFMPEG_MIN_ROOT_FOUND}/lib")

add_library(ffmpeg_min INTERFACE)
add_library(FFmpegMin::ffmpeg_min ALIAS ffmpeg_min)
target_include_directories(ffmpeg_min INTERFACE "${FFMPEG_MIN_INCLUDE_DIR}")

# Keep codec/bsf registration tables when statically linking.
if (APPLE)
    target_link_options(ffmpeg_min INTERFACE
        "LINKER:-force_load,${FFMPEG_MIN_LIB_AVFORMAT}"
        "LINKER:-force_load,${FFMPEG_MIN_LIB_AVCODEC}"
    )
    if (FFMPEG_MIN_LIB_SWRESAMPLE)
        target_link_options(ffmpeg_min INTERFACE
            "LINKER:-force_load,${FFMPEG_MIN_LIB_SWRESAMPLE}"
        )
    endif()
    target_link_options(ffmpeg_min INTERFACE
        "LINKER:-force_load,${FFMPEG_MIN_LIB_AVUTIL}"
    )
elseif (MSVC)
    # MSVC equivalent of force_load for archive members with registration tables.
    target_link_options(ffmpeg_min INTERFACE
        "/WHOLEARCHIVE:${FFMPEG_MIN_LIB_AVFORMAT}"
        "/WHOLEARCHIVE:${FFMPEG_MIN_LIB_AVCODEC}"
    )
    if (FFMPEG_MIN_LIB_SWRESAMPLE)
        target_link_options(ffmpeg_min INTERFACE
            "/WHOLEARCHIVE:${FFMPEG_MIN_LIB_SWRESAMPLE}"
        )
    endif()
    target_link_options(ffmpeg_min INTERFACE
        "/WHOLEARCHIVE:${FFMPEG_MIN_LIB_AVUTIL}"
    )
else()
    # Linux/ELF: --whole-archive ... --no-whole-archive
    set(_ff_link_libs
        -Wl,--whole-archive
        "${FFMPEG_MIN_LIB_AVFORMAT}"
        "${FFMPEG_MIN_LIB_AVCODEC}"
    )
    if (FFMPEG_MIN_LIB_SWRESAMPLE)
        list(APPEND _ff_link_libs "${FFMPEG_MIN_LIB_SWRESAMPLE}")
    endif()
    list(APPEND _ff_link_libs
        "${FFMPEG_MIN_LIB_AVUTIL}"
        -Wl,--no-whole-archive
    )
    target_link_libraries(ffmpeg_min INTERFACE ${_ff_link_libs})
endif()

# System deps commonly required by static FFmpeg.
find_package(ZLIB)
if (ZLIB_FOUND)
    target_link_libraries(ffmpeg_min INTERFACE ZLIB::ZLIB)
else()
    find_library(ZLIB_LIBRARY z)
    if (ZLIB_LIBRARY)
        target_link_libraries(ffmpeg_min INTERFACE "${ZLIB_LIBRARY}")
    endif()
endif()

find_library(BZIP2_LIBRARY bz2)
if (BZIP2_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${BZIP2_LIBRARY}")
endif()

find_library(ICONV_LIBRARY iconv)
if (ICONV_LIBRARY)
    target_link_libraries(ffmpeg_min INTERFACE "${ICONV_LIBRARY}")
endif()

if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(ffmpeg_min INTERFACE Threads::Threads m dl)
endif()

if (WIN32)
    target_link_libraries(ffmpeg_min INTERFACE bcrypt secur32 ws2_32)
endif()

message(STATUS "Using minimal static FFmpeg: ${FFMPEG_MIN_ROOT_FOUND}")
