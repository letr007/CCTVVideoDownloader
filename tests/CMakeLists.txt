qt6_wrap_cpp(core_regression_tests_moc
    ${PROJECT_SOURCE_DIR}/src/head/apiservice.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadengine.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
    ${PROJECT_SOURCE_DIR}/src/head/concatworker.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadcoordinator.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadcoordinatorseams.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadprogresswindow.h
    ${PROJECT_SOURCE_DIR}/src/head/import.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_networkreply.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_networkaccessmanager.h
)

qt_add_executable(core_regression_tests
    regression_core_tests.cpp
    fakes/fake_networkreply.cpp
    fakes/fake_networkaccessmanager.cpp
    ${PROJECT_SOURCE_DIR}/src/source/config.cpp
    ${PROJECT_SOURCE_DIR}/src/source/directmediafinalizer.cpp
    ${PROJECT_SOURCE_DIR}/src/source/setting.cpp
    ${PROJECT_SOURCE_DIR}/src/source/apiservice.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadtask.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadengine.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadmodel.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadcoordinator.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloaddialog.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadprogresswindow.cpp
    ${PROJECT_SOURCE_DIR}/src/source/decryptworker.cpp
    ${PROJECT_SOURCE_DIR}/src/source/concatworker.cpp
    ${PROJECT_SOURCE_DIR}/src/source/ffmpegcliremuxer.cpp
    ${PROJECT_SOURCE_DIR}/src/source/mediacontainervalidator.cpp
    ${PROJECT_SOURCE_DIR}/src/source/mediafinalizer.cpp
    ${PROJECT_SOURCE_DIR}/src/source/tsmerger.cpp
    ${PROJECT_SOURCE_DIR}/src/source/import.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/import.ui
    ${core_regression_tests_moc}
    ${PROJECT_SOURCE_DIR}/src/head/config.h
    ${PROJECT_SOURCE_DIR}/src/head/directmediafinalizer.h
    ${PROJECT_SOURCE_DIR}/src/head/setting.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadengine.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadmodel.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadcoordinator.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadcoordinatorseams.h
    ${PROJECT_SOURCE_DIR}/src/head/downloaddialog.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadprogresswindow.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
    ${PROJECT_SOURCE_DIR}/src/head/concatworker.h
    ${PROJECT_SOURCE_DIR}/src/head/ffmpegcliremuxer.h
    ${PROJECT_SOURCE_DIR}/src/head/mediacontainervalidator.h
    ${PROJECT_SOURCE_DIR}/src/head/mediafinalizer.h
    ${PROJECT_SOURCE_DIR}/src/head/tsmerger.h
    ${PROJECT_SOURCE_DIR}/src/head/import.h
)

target_include_directories(core_regression_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/head
)

target_link_libraries(core_regression_tests
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Test
        Qt6::Network
    )

target_compile_definitions(core_regression_tests
    PRIVATE
        CORE_REGRESSION_TESTS
        PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(core_regression_tests PRIVATE /utf-8)
endif()

add_custom_command(TARGET core_regression_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:core_regression_tests>/decrypt"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/src/decrypt"
            "$<TARGET_FILE_DIR:core_regression_tests>/decrypt"
)

add_test(NAME core_regression_tests COMMAND core_regression_tests)
set_tests_properties(core_regression_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
