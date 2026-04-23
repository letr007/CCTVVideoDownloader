qt6_wrap_cpp(core_regression_tests_moc
    ${PROJECT_SOURCE_DIR}/src/head/apiservice.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
)

qt_add_executable(core_regression_tests
    regression_core_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/source/config.cpp
    ${PROJECT_SOURCE_DIR}/src/source/setting.cpp
    ${PROJECT_SOURCE_DIR}/src/source/apiservice.cpp
    ${PROJECT_SOURCE_DIR}/src/source/downloadtask.cpp
    ${PROJECT_SOURCE_DIR}/src/source/decryptworker.cpp
    ${core_regression_tests_moc}
    ${PROJECT_SOURCE_DIR}/src/head/config.h
    ${PROJECT_SOURCE_DIR}/src/head/setting.h
    ${PROJECT_SOURCE_DIR}/src/head/downloadtask.h
    ${PROJECT_SOURCE_DIR}/src/head/decryptworker.h
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
