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

set(PROJECT_SOURCES
    src/source/about.cpp
    src/source/apiservice.cpp
    src/source/cctvvideodownloader.cpp
    src/source/concat.cpp
    src/source/concatworker.cpp
    src/source/config.cpp
    src/source/decrypt.cpp
    src/source/decryptworker.cpp
    src/source/downloaddialog.cpp
    src/source/downloadengine.cpp
    src/source/downloadmodel.cpp
    src/source/downloadtask.cpp
    src/source/import.cpp
    src/source/logger.cpp
    src/source/main.cpp
    src/source/setting.cpp
    src/source/tsmerger.cpp

    src/head/about.h
    src/head/apiservice.h
    src/head/cctvvideodownloader.h
    src/head/concat.h
    src/head/concatworker.h
    src/head/config.h
    src/head/decrypt.h
    src/head/decryptworker.h
    src/head/downloadengine.h
    src/head/downloaddialog.h
    src/head/downloadmodel.h
    src/head/downloadtask.h
    src/head/import.h
    src/head/logger.h
    src/head/setting.h
    src/head/tsmerger.h

    src/ui/about.ui
    src/ui/cctvvideodownloader.ui
    src/ui/ctvd.ui
    src/ui/ctvd_setting.ui
    src/ui/dialog.ui
    src/ui/import.ui
    src/ui/process.ui

    src/resources/cctvvideodownloader.qrc
    src/resources/resources.qrc
    src/resources/CCTVVideoDownloader.rc
)

qt_add_executable(CCTVVideoDownloader
    WIN32
    ${PROJECT_SOURCES}
)

target_include_directories(CCTVVideoDownloader
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/head
        ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
)

target_link_libraries(CCTVVideoDownloader
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

add_custom_command(TARGET CCTVVideoDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CCTVVideoDownloader>/decrypt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/decrypt"
            "$<TARGET_FILE_DIR:CCTVVideoDownloader>/decrypt"
)
