cmake_minimum_required(VERSION 3.21)

project(CCTVVideoDownloader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# Minimal static FFmpeg for in-process TS->MP4 remux (no system ffmpeg CLI).
# Provides FFmpegMin::ffmpeg_min (and alias ffmpeg_min).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindFFmpegMin)

# CTest defines option(BUILD_TESTING) with default ON. Include it before the
# conditional so a bare configure actually enables tests unless -DBUILD_TESTING=OFF.
include(CTest)
if (BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

set(PROJECT_SOURCES
    src/source/about.cpp
    src/source/apiservice.cpp
    src/source/cctvvideodownloader.cpp
    src/source/concatworker.cpp
    src/source/config.cpp
    src/source/downloadcoordinator.cpp
    src/source/directmediafinalizer.cpp
    src/source/decryptworker.cpp
    src/source/downloaddialog.cpp
    src/source/downloadengine.cpp
    src/source/downloadmodel.cpp
    src/source/downloadprogresswindow.cpp
    src/source/downloadtask.cpp
    src/source/import.cpp
    src/source/logger.cpp
    src/source/mediacontainervalidator.cpp
    src/source/mediafinalizer.cpp
    src/source/libavremuxer.cpp
    src/source/main.cpp
    src/source/setting.cpp
    src/source/tsmerger.cpp
    src/crypto/cctv_h5e_decrypt.cpp

    src/head/about.h
    src/head/apiservice.h
    src/head/cctvvideodownloader.h
    src/head/concatworker.h
    src/head/config.h
    src/head/downloadcoordinator.h
    src/head/directmediafinalizer.h
    src/head/downloadcoordinatorseams.h
    src/head/decryptworker.h
    src/head/downloadengine.h
    src/head/downloaddialog.h
    src/head/downloadmodel.h
    src/head/downloadprogresswindow.h
    src/head/downloadtask.h
    src/head/import.h
    src/head/logger.h
    src/head/mediacontainervalidator.h
    src/head/mediafinalizer.h
    src/head/libavremuxer.h
    src/head/setting.h
    src/head/tsmerger.h

    src/ui/about.ui
    src/ui/cctvvideodownloader.ui
    src/ui/ctvd.ui
    src/ui/ctvd_setting.ui
    src/ui/dialog.ui
    src/ui/import.ui

    src/resources/cctvvideodownloader.qrc
    src/resources/resources.qrc
    src/resources/CCTVVideoDownloader.rc
)

qt_add_executable(CCTVVideoDownloader
    WIN32
    MACOSX_BUNDLE
    ${PROJECT_SOURCES}
)

target_include_directories(CCTVVideoDownloader
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/head
        ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
        ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto
)

# Includes and whole-archive static libs come from the imported target.
target_link_libraries(CCTVVideoDownloader
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        FFmpegMin::ffmpeg_min
)

if (MSVC)
    target_compile_options(CCTVVideoDownloader PRIVATE /utf-8)
endif()

# Native Qt deployment produces install trees for the macOS and Linux release
# archives. Windows keeps its explicit windeployqt release staging below.
if (APPLE OR (UNIX AND NOT APPLE))
    install(TARGETS CCTVVideoDownloader
        BUNDLE DESTINATION .
        RUNTIME DESTINATION bin
    )
    qt_generate_deploy_app_script(
        TARGET CCTVVideoDownloader
        OUTPUT_SCRIPT CCTVVideoDownloader_DEPLOY_SCRIPT
        NO_TRANSLATIONS
    )
    install(SCRIPT "${CCTVVideoDownloader_DEPLOY_SCRIPT}")
endif()

if (WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET CCTVVideoDownloader POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:CCTVVideoDownloader>"
            COMMENT "Running windeployqt for CCTVVideoDownloader"
        )
    else()
        message(WARNING "windeployqt not found; the built executable may miss Qt runtime DLLs.")
    endif()
endif()
