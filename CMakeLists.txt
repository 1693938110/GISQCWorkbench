cmake_minimum_required(VERSION 3.20)
project(GISQCWorkbench VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

option(GISQC_BUILD_QT_APP "Build Qt desktop application" ON)
option(GISQC_BUILD_CLI "Build command line preview executable" ON)
option(GISQC_BUILD_WIN_PREVIEW "Build native Win32 preview application" ON)
option(GISQC_BUILD_TESTS "Build core tests" ON)
option(GISQC_WITH_GDAL "Enable GDAL-backed vector metadata reading" ON)

if(GISQC_WITH_GDAL)
    find_package(GDAL QUIET)
endif()

add_library(gisqc_core
    src/core/DatasetScanner.cpp
    src/core/DatasetMetadataReader.cpp
    src/core/DatasetScanService.cpp
    src/core/RuleTemplateLoader.cpp
    src/core/RuleTemplateStore.cpp
    src/core/TaskModel.cpp
    src/core/ResultStatistics.cpp
    src/core/IssueCsvExporter.cpp
    src/core/IssuePreviewExporter.cpp
    src/core/ReportExporter.cpp
    src/core/TaskHistoryStore.cpp
    src/core/RuleCheckEngine.cpp
    src/core/TaskSession.cpp
    src/core/LicenseManager.cpp
)
target_include_directories(gisqc_core PUBLIC src)
if(GDAL_FOUND)
    target_compile_definitions(gisqc_core PUBLIC GISQC_HAVE_GDAL)
    target_link_libraries(gisqc_core PUBLIC GDAL::GDAL)
endif()

if(GISQC_BUILD_TESTS)
    enable_testing()
    add_executable(test_dataset_scanner tests/test_dataset_scanner.cpp)
    target_link_libraries(test_dataset_scanner PRIVATE gisqc_core)
    add_test(NAME DatasetScanner COMMAND test_dataset_scanner)

    add_executable(test_rule_template_loader tests/test_rule_template_loader.cpp)
    target_link_libraries(test_rule_template_loader PRIVATE gisqc_core)
    add_test(NAME RuleTemplateLoader COMMAND test_rule_template_loader)

    add_executable(test_task_model tests/test_task_model.cpp)
    target_link_libraries(test_task_model PRIVATE gisqc_core)
    add_test(NAME TaskModel COMMAND test_task_model)

    add_executable(test_dataset_metadata_reader tests/test_dataset_metadata_reader.cpp)
    target_link_libraries(test_dataset_metadata_reader PRIVATE gisqc_core)
    add_test(NAME DatasetMetadataReader COMMAND test_dataset_metadata_reader)

    add_executable(test_dataset_scan_service tests/test_dataset_scan_service.cpp)
    target_link_libraries(test_dataset_scan_service PRIVATE gisqc_core)
    add_test(NAME DatasetScanService COMMAND test_dataset_scan_service)

    add_executable(test_result_statistics tests/test_result_statistics.cpp)
    target_link_libraries(test_result_statistics PRIVATE gisqc_core)
    add_test(NAME ResultStatistics COMMAND test_result_statistics)

    add_executable(test_issue_csv_exporter tests/test_issue_csv_exporter.cpp)
    target_link_libraries(test_issue_csv_exporter PRIVATE gisqc_core)
    add_test(NAME IssueCsvExporter COMMAND test_issue_csv_exporter)

    add_executable(test_report_exporter tests/test_report_exporter.cpp)
    target_link_libraries(test_report_exporter PRIVATE gisqc_core)
    add_test(NAME ReportExporter COMMAND test_report_exporter)

    add_executable(test_rule_check_engine tests/test_rule_check_engine.cpp)
    target_link_libraries(test_rule_check_engine PRIVATE gisqc_core)
    add_test(NAME RuleCheckEngine COMMAND test_rule_check_engine)

    add_executable(test_task_session tests/test_task_session.cpp)
    target_link_libraries(test_task_session PRIVATE gisqc_core)
    add_test(NAME TaskSession COMMAND test_task_session)

    add_executable(test_task_history_store tests/test_task_history_store.cpp)
    target_link_libraries(test_task_history_store PRIVATE gisqc_core)
    add_test(NAME TaskHistoryStore COMMAND test_task_history_store)

    add_executable(test_license_manager tests/test_license_manager.cpp)
    target_link_libraries(test_license_manager PRIVATE gisqc_core)
    add_test(NAME LicenseManager COMMAND test_license_manager)

    add_executable(test_execution_page_integration tests/test_execution_page_integration.cpp)
    add_test(NAME ExecutionPageIntegration COMMAND test_execution_page_integration)

    add_executable(test_license_registration_dialog tests/test_license_registration_dialog.cpp)
    add_test(NAME LicenseRegistrationDialog COMMAND test_license_registration_dialog)

    set_tests_properties(
        DatasetScanner
        RuleTemplateLoader
        TaskModel
        DatasetMetadataReader
        DatasetScanService
        ResultStatistics
        IssueCsvExporter
        ReportExporter
        RuleCheckEngine
        TaskSession
        TaskHistoryStore
        LicenseManager
        ExecutionPageIntegration
        LicenseRegistrationDialog
        PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()

if(GISQC_BUILD_CLI)
    add_executable(GISQCWorkbenchCLI
        src/cli/main.cpp
    )
    target_link_libraries(GISQCWorkbenchCLI PRIVATE gisqc_core)

    add_executable(LicenseGenerator
        src/license_generator/main.cpp
    )
    target_link_libraries(LicenseGenerator PRIVATE gisqc_core)
endif()

if(GISQC_WITH_GDAL AND GDAL_FOUND)
    add_executable(qa_generate_synthetic_data qa_generate_synthetic_data.cpp)
    target_link_libraries(qa_generate_synthetic_data PRIVATE GDAL::GDAL)
endif()

if(WIN32 AND GISQC_BUILD_WIN_PREVIEW)
    add_executable(GISQCWorkbenchWinPreview WIN32
        src/winpreview/main.cpp
    )
    target_link_libraries(GISQCWorkbenchWinPreview PRIVATE gisqc_core shell32 ole32)
endif()

if(GISQC_BUILD_QT_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()

    qt_add_executable(GISQCWorkbench
        src/app/main.cpp
        src/ui/MainWindow.cpp
        src/ui/MainWindow.h
        src/ui/HomePage.cpp
        src/ui/HomePage.h
        src/ui/DashboardPage.cpp
        src/ui/DashboardPage.h
        src/ui/RuleConfigPage.cpp
        src/ui/RuleConfigPage.h
        src/ui/ExecutionPage.cpp
        src/ui/ExecutionPage.h
        src/ui/QualityCheckWorker.cpp
        src/ui/QualityCheckWorker.h
        src/ui/ResultsPage.cpp
        src/ui/ResultsPage.h
        src/ui/TemplateManagePage.cpp
        src/ui/TemplateManagePage.h
        src/ui/SettingsPage.cpp
        src/ui/SettingsPage.h
        src/resources/resources.qrc
        src/resources/app_icon.rc
    )
    target_link_libraries(GISQCWorkbench PRIVATE Qt6::Widgets gisqc_core)
    set_target_properties(GISQCWorkbench PROPERTIES WIN32_EXECUTABLE TRUE)

    # Copy QSS to output directory so filesystem loading always picks up latest
    add_custom_command(TARGET GISQCWorkbench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/resources/app.qss"
            "$<TARGET_FILE_DIR:GISQCWorkbench>/app.qss"
        COMMENT "Copying app.qss to output directory"
    )
endif()
