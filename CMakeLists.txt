cmake_minimum_required(VERSION 3.21)

project(CCTVVideoDownloader VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/ui
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)

# Keep the in-app version aligned with the source actually being built. Tagged
# release builds show their tag; local builds fall back to a commit identifier.
find_package(Git QUIET)
set(CCTV_VIDEO_DOWNLOADER_BUILD_VERSION "dev")
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --always --dirty
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE CCTV_VIDEO_DOWNLOADER_GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (NOT CCTV_VIDEO_DOWNLOADER_GIT_VERSION STREQUAL "")
        set(CCTV_VIDEO_DOWNLOADER_BUILD_VERSION "${CCTV_VIDEO_DOWNLOADER_GIT_VERSION}")
    endif()
endif()

# Minimal static FFmpeg for in-process TS->MP4 remux (no system ffmpeg CLI).
# Provides FFmpegMin::ffmpeg_min (and alias ffmpeg_min).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindFFmpegMin)

set(CORE_SOURCES
    src/core/src/apiservice.cpp
    src/core/src/concatworker.cpp
    src/core/src/config.cpp
    src/core/src/decryptworker.cpp
    src/core/src/directmediafinalizer.cpp
    src/core/src/downloadcoordinator.cpp
    src/core/src/downloadengine.cpp
    src/core/src/downloadtask.cpp
    src/core/src/libavremuxer.cpp
    src/core/src/logger.cpp
    src/core/src/mediacontainervalidator.cpp
    src/core/src/mediafinalizer.cpp
    src/core/src/tsmerger.cpp
    src/core/src/parse/contentparse.cpp
    src/core/src/parse/contentresolver.cpp
    src/core/src/crypto/cctv_h5e_decrypt.cpp

    src/core/include/apiservice.h
    src/core/include/concatworker.h
    src/core/include/config.h
    src/core/include/contentparse.h
    src/core/include/contentresolver.h
    src/core/include/decryptworker.h
    src/core/include/directmediafinalizer.h
    src/core/include/downloadcoordinator.h
    src/core/include/downloadcoordinatorseams.h
    src/core/include/downloadengine.h
    src/core/include/downloadinfo.h
    src/core/include/downloadjob.h
    src/core/include/downloadtask.h
    src/core/include/libavremuxer.h
    src/core/include/logger.h
    src/core/include/mediacontainervalidator.h
    src/core/include/mediafinalizer.h
    src/core/include/tsmerger.h
)

add_library(CCTVVideoDownloaderCore STATIC ${CORE_SOURCES})
target_include_directories(CCTVVideoDownloaderCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/src/crypto
)
target_link_libraries(CCTVVideoDownloaderCore
    PUBLIC
        Qt6::Core
        Qt6::Network
        FFmpegMin::ffmpeg_min
)

set(GUI_SOURCES
    src/gui/src/about.cpp
    src/gui/src/cctvvideodownloader.cpp
    src/gui/src/downloaddialog.cpp
    src/gui/src/downloadmodel.cpp
    src/gui/src/downloadprogresswindow.cpp
    src/gui/src/imageloader.cpp
    src/gui/src/import.cpp
    src/gui/src/main.cpp
    src/gui/src/monthcalendarwidget.cpp
    src/gui/src/setting.cpp
    src/gui/src/theme.cpp

    src/gui/include/about.h
    src/gui/include/cctvvideodownloader.h
    src/gui/include/downloaddialog.h
    src/gui/include/downloadmodel.h
    src/gui/include/downloadprogresswindow.h
    src/gui/include/imageloader.h
    src/gui/include/import.h
    src/gui/include/monthcalendarwidget.h
    src/gui/include/setting.h
    src/gui/include/theme.h

    src/gui/ui/about.ui
    src/gui/ui/cctvvideodownloader.ui
    src/gui/ui/ctvd.ui
    src/gui/ui/ctvd_setting.ui
    src/gui/ui/dialog.ui
    src/gui/ui/import.ui

    src/gui/resources/cctvvideodownloader.qrc
    src/gui/resources/resources.qrc
    src/gui/resources/CCTVVideoDownloader.rc
)

qt_add_executable(CCTVVideoDownloader
    WIN32
    MACOSX_BUNDLE
    ${GUI_SOURCES}
)

if (APPLE)
    set(CCTV_VIDEO_DOWNLOADER_MAC_ICON
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/resources/CCTVVideoDownloader.icns"
    )
    target_sources(CCTVVideoDownloader PRIVATE "${CCTV_VIDEO_DOWNLOADER_MAC_ICON}")
    set_source_files_properties("${CCTV_VIDEO_DOWNLOADER_MAC_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(CCTVVideoDownloader PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "CCTVVideoDownloader.icns"
        MACOSX_BUNDLE_BUNDLE_NAME "CCTVVideoDownloader"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.letr007.CCTVVideoDownloader"
    )
endif()

target_include_directories(CCTVVideoDownloader
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
)
target_link_libraries(CCTVVideoDownloader
    PRIVATE
        CCTVVideoDownloaderCore
        Qt6::Gui
        Qt6::Widgets
)
target_compile_definitions(CCTVVideoDownloader
    PRIVATE CCTV_VIDEO_DOWNLOADER_BUILD_VERSION="${CCTV_VIDEO_DOWNLOADER_BUILD_VERSION}"
)

qt_add_executable(cctv-dl
    src/cli/main.cpp
    src/cli/cli_controller.cpp
    src/cli/cli_controller.h
    src/cli/cli_logging.cpp
    src/cli/cli_logging.h
    src/cli/cli_output.cpp
    src/cli/cli_output.h
    src/cli/cli_parser.cpp
    src/cli/cli_parser.h
    src/cli/cli_support.cpp
    src/cli/cli_support.h
    src/cli/signal_bridge.cpp
    src/cli/signal_bridge.h
)
target_link_libraries(cctv-dl
    PRIVATE
        CCTVVideoDownloaderCore
)
target_compile_definitions(cctv-dl
    PRIVATE CCTV_VIDEO_DOWNLOADER_BUILD_VERSION="${CCTV_VIDEO_DOWNLOADER_BUILD_VERSION}"
)
if (UNIX AND NOT APPLE)
    set_target_properties(cctv-dl PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

if (MSVC)
    target_compile_options(CCTVVideoDownloaderCore PRIVATE /utf-8)
    target_compile_options(CCTVVideoDownloader PRIVATE /utf-8)
    target_compile_options(cctv-dl PRIVATE /utf-8)
endif()

# CTest defines option(BUILD_TESTING) with default ON. Include it before the
# conditional so a bare configure actually enables tests unless -DBUILD_TESTING=OFF.
include(CTest)
if (BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    target_compile_definitions(CCTVVideoDownloaderCore PRIVATE CORE_REGRESSION_TESTS)
    add_subdirectory(tests)
endif()

# Native Qt deployment produces separate GUI and CLI install trees on macOS and
# Linux. Windows keeps its explicit windeployqt release staging below.
install(TARGETS cctv-dl
    RUNTIME DESTINATION bin COMPONENT cli
)

if (UNIX AND NOT APPLE)
    qt_generate_deploy_app_script(
        TARGET cctv-dl
        OUTPUT_SCRIPT CCTV_DL_DEPLOY_SCRIPT
        NO_TRANSLATIONS
    )
    install(SCRIPT "${CCTV_DL_DEPLOY_SCRIPT}" COMPONENT cli)
endif()

if (APPLE OR (UNIX AND NOT APPLE))
    install(TARGETS CCTVVideoDownloader
        BUNDLE DESTINATION . COMPONENT gui
        RUNTIME DESTINATION bin COMPONENT gui
    )
    qt_generate_deploy_app_script(
        TARGET CCTVVideoDownloader
        OUTPUT_SCRIPT CCTVVideoDownloader_DEPLOY_SCRIPT
        NO_TRANSLATIONS
    )
    install(SCRIPT "${CCTVVideoDownloader_DEPLOY_SCRIPT}" COMPONENT gui)
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
