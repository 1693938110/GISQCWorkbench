PROJECT_ROOT := $(CURDIR)
CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -I$(PROJECT_ROOT)/src
GDAL_CFLAGS := $(shell gdal-config --cflags 2>/dev/null)
GDAL_LIBS := $(shell gdal-config --libs 2>/dev/null)
GDAL_DEFS := $(if $(GDAL_CFLAGS),-DGISQC_HAVE_GDAL,)

TEST_BINS := \
	/tmp/gis_qc_test_dataset_scanner \
	/tmp/gis_qc_test_rule_template_loader \
	/tmp/gis_qc_test_task_model \
	/tmp/gis_qc_test_dataset_metadata_reader \
	/tmp/gis_qc_test_dataset_scan_service \
	/tmp/gis_qc_test_result_statistics \
	/tmp/gis_qc_test_issue_csv_exporter \
	/tmp/gis_qc_test_rule_check_engine \
	/tmp/gis_qc_test_task_session \
	/tmp/gis_qc_test_execution_page_integration

.PHONY: test-core clean

test-core: $(TEST_BINS)
	/tmp/gis_qc_test_dataset_scanner
	/tmp/gis_qc_test_rule_template_loader
	/tmp/gis_qc_test_task_model
	/tmp/gis_qc_test_dataset_metadata_reader
	/tmp/gis_qc_test_dataset_scan_service
	/tmp/gis_qc_test_result_statistics
	/tmp/gis_qc_test_issue_csv_exporter
	/tmp/gis_qc_test_rule_check_engine
	/tmp/gis_qc_test_task_session
	/tmp/gis_qc_test_execution_page_integration

/tmp/gis_qc_test_dataset_scanner: tests/test_dataset_scanner.cpp src/core/DatasetScanner.cpp src/core/DatasetScanner.h
	$(CXX) $(CXXFLAGS) tests/test_dataset_scanner.cpp src/core/DatasetScanner.cpp -o $@

/tmp/gis_qc_test_rule_template_loader: tests/test_rule_template_loader.cpp src/core/RuleTemplateLoader.cpp src/core/RuleTemplateLoader.h src/core/RuleTemplateStore.cpp src/core/RuleTemplateStore.h src/core/RuleDefinition.h
	$(CXX) $(CXXFLAGS) tests/test_rule_template_loader.cpp src/core/RuleTemplateLoader.cpp src/core/RuleTemplateStore.cpp -o $@

/tmp/gis_qc_test_task_model: tests/test_task_model.cpp src/core/TaskModel.cpp src/core/TaskModel.h
	$(CXX) $(CXXFLAGS) tests/test_task_model.cpp src/core/TaskModel.cpp -o $@

/tmp/gis_qc_test_dataset_metadata_reader: tests/test_dataset_metadata_reader.cpp src/core/DatasetMetadataReader.cpp src/core/DatasetMetadataReader.h src/core/DatasetScanner.cpp src/core/DatasetScanner.h
	$(CXX) $(CXXFLAGS) $(GDAL_DEFS) $(GDAL_CFLAGS) tests/test_dataset_metadata_reader.cpp src/core/DatasetMetadataReader.cpp src/core/DatasetScanner.cpp $(GDAL_LIBS) -o $@

/tmp/gis_qc_test_dataset_scan_service: tests/test_dataset_scan_service.cpp src/core/DatasetScanService.cpp src/core/DatasetScanner.cpp src/core/DatasetMetadataReader.cpp src/core/DatasetScanService.h src/core/DatasetScanner.h src/core/DatasetMetadataReader.h
	$(CXX) $(CXXFLAGS) $(GDAL_DEFS) $(GDAL_CFLAGS) tests/test_dataset_scan_service.cpp src/core/DatasetScanService.cpp src/core/DatasetScanner.cpp src/core/DatasetMetadataReader.cpp $(GDAL_LIBS) -o $@

/tmp/gis_qc_test_result_statistics: tests/test_result_statistics.cpp src/core/ResultStatistics.cpp src/core/ResultStatistics.h src/core/IssueRecord.h
	$(CXX) $(CXXFLAGS) tests/test_result_statistics.cpp src/core/ResultStatistics.cpp -o $@

/tmp/gis_qc_test_issue_csv_exporter: tests/test_issue_csv_exporter.cpp src/core/IssueCsvExporter.cpp src/core/IssueCsvExporter.h src/core/IssueRecord.h
	$(CXX) $(CXXFLAGS) tests/test_issue_csv_exporter.cpp src/core/IssueCsvExporter.cpp -o $@

/tmp/gis_qc_test_rule_check_engine: tests/test_rule_check_engine.cpp src/core/RuleCheckEngine.cpp src/core/RuleCheckEngine.h src/core/RuleDefinition.h src/core/IssueRecord.h src/core/DatasetScanner.cpp src/core/DatasetScanner.h src/core/DatasetMetadataReader.cpp src/core/DatasetMetadataReader.h
	$(CXX) $(CXXFLAGS) $(GDAL_DEFS) $(GDAL_CFLAGS) tests/test_rule_check_engine.cpp src/core/RuleCheckEngine.cpp src/core/DatasetScanner.cpp src/core/DatasetMetadataReader.cpp $(GDAL_LIBS) -o $@

/tmp/gis_qc_test_task_session: tests/test_task_session.cpp src/core/TaskSession.cpp src/core/TaskSession.h src/core/TaskModel.cpp src/core/DatasetScanService.cpp src/core/DatasetScanner.cpp src/core/DatasetMetadataReader.cpp src/core/RuleCheckEngine.cpp src/core/ResultStatistics.cpp
	$(CXX) $(CXXFLAGS) $(GDAL_DEFS) $(GDAL_CFLAGS) tests/test_task_session.cpp src/core/TaskSession.cpp src/core/TaskModel.cpp src/core/DatasetScanService.cpp src/core/DatasetScanner.cpp src/core/DatasetMetadataReader.cpp src/core/RuleCheckEngine.cpp src/core/ResultStatistics.cpp $(GDAL_LIBS) -o $@

/tmp/gis_qc_test_execution_page_integration: tests/test_execution_page_integration.cpp
	$(CXX) $(CXXFLAGS) tests/test_execution_page_integration.cpp -o $@

clean:
	rm -f $(TEST_BINS)
