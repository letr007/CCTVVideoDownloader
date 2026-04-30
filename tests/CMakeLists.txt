qt6_wrap_cpp(core_regression_tests_moc
    ${PROJECT_SOURCE_DIR}/src/head/apiservice.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadengine.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
    ${PROJECT_SOURCE_DIR}/src/head/concatworker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_networkreply.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fakes/fake_networkaccessmanager.h
)

qt_add_executable(core_regression_tests
    regression_core_tests.cpp
    fakes/fake_networkreply.cpp
    fakes/fake_networkaccessmanager.cpp
    ${PROJECT_SOURCE_DIR}/src/source/config.cpp
    ${PROJECT_SOURCE_DIR}/src/source/setting.cpp
    ${PROJECT_SOURCE_DIR}/src/source/apiservice.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadtask.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadengine.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadmodel.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloaddialog.cpp
    ${PROJECT_SOURCE_DIR}/src/source/decryptworker.cpp
    ${PROJECT_SOURCE_DIR}/src/source/concatworker.cpp
    ${PROJECT_SOURCE_DIR}/src/source/tsmerger.cpp
    ${core_regression_tests_moc}
    ${PROJECT_SOURCE_DIR}/src/head/config.h
    ${PROJECT_SOURCE_DIR}/src/head/setting.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadengine.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadmodel.h
    ${PROJECT_SOURCE_DIR}/src/head/downloaddialog.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
    ${PROJECT_SOURCE_DIR}/src/head/concatworker.h
    ${PROJECT_SOURCE_DIR}/src/head/tsmerger.h
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
)

add_test(NAME core_regression_tests COMMAND core_regression_tests)
