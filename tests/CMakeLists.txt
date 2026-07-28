qt_add_executable(core_regression_tests
    regression_core_tests.cpp
    fakes/fake_networkreply.cpp
    fakes/fake_networkaccessmanager.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/imageloader.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/setting.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/theme.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/downloadmodel.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/downloaddialog.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/downloadprogresswindow.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/import.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/src/monthcalendarwidget.cpp
    ${PROJECT_SOURCE_DIR}/src/gui/ui/import.ui
    ${PROJECT_SOURCE_DIR}/src/gui/include/imageloader.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/setting.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/theme.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/downloadmodel.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/downloaddialog.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/downloadprogresswindow.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/import.h
    ${PROJECT_SOURCE_DIR}/src/gui/include/monthcalendarwidget.h
)

target_include_directories(core_regression_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/core/include
        ${PROJECT_SOURCE_DIR}/src/core/src/crypto
        ${PROJECT_SOURCE_DIR}/src/gui/include
)

target_link_libraries(core_regression_tests
    PRIVATE
        CCTVVideoDownloaderCore
        Qt6::Gui
        Qt6::Widgets
        Qt6::Test
)

target_compile_definitions(core_regression_tests
    PRIVATE
        CORE_REGRESSION_TESTS
        PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(core_regression_tests PRIVATE /utf-8)
endif()

qt_add_executable(core_headless_tests
    core_headless_tests.cpp
)
target_link_libraries(core_headless_tests
    PRIVATE
        CCTVVideoDownloaderCore
        Qt6::Test
)

qt_add_executable(cli_tests
    cli_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/cli_logging.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/cli_logging.h
    ${PROJECT_SOURCE_DIR}/src/cli/cli_output.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/cli_output.h
    ${PROJECT_SOURCE_DIR}/src/cli/cli_support.cpp
    ${PROJECT_SOURCE_DIR}/src/cli/cli_support.h
)
target_include_directories(cli_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/cli
        ${PROJECT_SOURCE_DIR}/src/core/include
)
target_link_libraries(cli_tests
    PRIVATE
        FFmpegMin::ffmpeg_min
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(cli_tests
    PRIVATE CLI_EXECUTABLE="$<TARGET_FILE:cctv-video>"
)
add_dependencies(cli_tests cctv-video)
if (MSVC)
    target_compile_options(core_headless_tests PRIVATE /utf-8)
    target_compile_options(cli_tests PRIVATE /utf-8)
endif()

add_test(NAME core_regression_tests COMMAND core_regression_tests)
add_test(NAME core_headless_tests COMMAND core_headless_tests)
add_test(NAME cli_tests COMMAND cli_tests)
set_tests_properties(core_regression_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    REPEAT UNTIL_PASS:3
)
