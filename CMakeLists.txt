cmake_minimum_required(VERSION 3.21)

project(
    fooyin_drmeter
    VERSION 0.1.0
    DESCRIPTION "Offline Dynamic Range (TT DR14) meter plugin for fooyin"
    LANGUAGES CXX
)

include(CTest)

find_package(Fooyin REQUIRED)

add_library(
    drmeter_core
    STATIC
        src/core/dranalyzer.cpp
        src/core/dranalyzer.h
        src/core/drlogformatter.cpp
        src/core/drlogformatter.h
        src/core/drtypes.h
)

target_compile_features(drmeter_core PUBLIC cxx_std_23)
target_include_directories(drmeter_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(
    drmeter_core
    PROPERTIES
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drmeter_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(
    drmeter_plugin_logic
    STATIC
        src/plugin/dralbumgrouper.cpp
        src/plugin/dralbumgrouper.h
        src/plugin/drlogexporter.cpp
        src/plugin/drlogexporter.h
        src/plugin/drresultsmodel.cpp
        src/plugin/drresultsmodel.h
        src/plugin/drscanner.cpp
        src/plugin/drscanner.h
        src/plugin/drsettings.cpp
        src/plugin/drsettings.h
        src/plugin/drtagwriter.cpp
        src/plugin/drtagwriter.h
)

target_compile_features(drmeter_plugin_logic PUBLIC cxx_std_23)
target_include_directories(drmeter_plugin_logic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(
    drmeter_plugin_logic
    PUBLIC
        QT_USE_QSTRINGBUILDER
    PRIVATE
        FOOYIN_SDK_VERSION="${Fooyin_VERSION}"
        DRMETER_PLUGIN_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
    drmeter_plugin_logic
    PUBLIC
        drmeter_core
        Fooyin::Core
)
set_target_properties(
    drmeter_plugin_logic
    PROPERTIES
        AUTOMOC ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drmeter_plugin_logic PRIVATE -Wall -Wextra -Wpedantic)
endif()

create_fooyin_plugin(
    drmeter
    JSON_IN src/plugin/drmeter.json.in
    DEPENDS
        drmeter_plugin_logic
        Fooyin::Core
        Fooyin::Gui
    SOURCES
        src/plugin/drmeterplugin.cpp
        src/plugin/drmeterplugin.h
        src/plugin/drprogressdialog.cpp
        src/plugin/drprogressdialog.h
        src/plugin/drresultsdialog.cpp
        src/plugin/drresultsdialog.h
)

target_compile_features(drmeter PRIVATE cxx_std_23)
set_target_properties(
    drmeter
    PROPERTIES
        CXX_EXTENSIONS OFF
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(drmeter PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(
        drmeter_core_tests
        tests/dranalyzer_test.cpp
        tests/drlogformatter_test.cpp
        tests/smoke_test.cpp
    )
    target_link_libraries(drmeter_core_tests PRIVATE drmeter_core GTest::gtest_main)
    target_compile_features(drmeter_core_tests PRIVATE cxx_std_23)
    set_target_properties(drmeter_core_tests PROPERTIES CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(drmeter_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    gtest_discover_tests(drmeter_core_tests)

    add_executable(
        drmeter_integration_tests
        tests/integration_scanner_test.cpp
        tests/ui_model_export_test.cpp
    )
    target_link_libraries(drmeter_integration_tests PRIVATE drmeter_plugin_logic GTest::gtest)
    target_compile_features(drmeter_integration_tests PRIVATE cxx_std_23)
    set_target_properties(drmeter_integration_tests PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(drmeter_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    gtest_discover_tests(drmeter_integration_tests)
endif()
