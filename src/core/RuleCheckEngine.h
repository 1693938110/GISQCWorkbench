#pragma once

#include "IssueRecord.h"
#include "RuleDefinition.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gisqc {

class RuleCheckEngine {
public:
    std::vector<IssueRecord> check(const std::string& rootPath, const std::vector<RuleDefinition>& rules) const;

private:
    struct FieldInfo {
        std::string tableName;
        std::string fieldName;
        std::string type;
        int width{0};
    };

    struct FieldValueRecord {
        std::string tableName;
        std::string featureId;
        std::map<std::string, std::string> values;
    };

    static IssueRecord makeIssue(int issueIndex, const RuleDefinition& rule, const std::string& layerName, const std::string& featureId, const std::string& description);
    static IssueRecord makeGeometryIssue(int issueIndex, const RuleDefinition& rule, const std::string& layerName, const std::string& featureId, const std::string& description, const std::string& sourcePath, const void* geometry);
    static Severity severityFromString(const std::string& severity);
    static bool truthyParameter(const RuleDefinition& rule, const std::string& name, bool defaultValue = false);
    static std::vector<std::string> csvParameter(const RuleDefinition& rule, const std::string& name, const std::string& defaultValue = "");
    static std::vector<std::string> semicolonParameter(const RuleDefinition& rule, const std::string& name);
    static std::map<std::string, std::string> assignmentParameter(const RuleDefinition& rule, const std::string& name);
    static bool hasSupportedDataset(const std::filesystem::path& rootPath);
    static std::vector<std::string> availableLayerNames(const std::filesystem::path& rootPath);
    static std::vector<FieldInfo> availableFields(const std::filesystem::path& rootPath);
    static std::vector<FieldInfo> readDbfFields(const std::string& shpPath, const std::string& tableName);
    static std::vector<FieldValueRecord> availableRecords(const std::filesystem::path& rootPath);
    static std::vector<FieldValueRecord> readDbfRecords(const std::string& shpPath, const std::string& tableName);
    static bool hasField(const std::vector<FieldInfo>& fields, const std::string& tableName, const std::string& fieldName);
    static const FieldInfo* findField(const std::vector<FieldInfo>& fields, const std::string& tableName, const std::string& fieldName);
    static void appendTableStructureIssues(const std::filesystem::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex);
    static void appendAttributeValueIssues(const std::filesystem::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex);
    static bool containsName(const std::vector<std::string>& names, const std::string& expected);
    static bool parseFieldReference(const std::string& reference, std::string& tableName, std::string& fieldName);
    static std::vector<std::string> splitValueList(const std::string& value, char delimiter);
    static const std::string* recordValue(const FieldValueRecord& record, const std::string& fieldName);
    static bool valueInList(const std::string& value, const std::vector<std::string>& allowedValues);
    static void appendCoordinateIssues(const std::filesystem::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex);
    static void appendGdalGeometryIssues(const std::filesystem::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex);
    static void appendInterLayerTopologyIssues(const std::filesystem::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex);
    static bool isDirectoryEmpty(const std::filesystem::path& path);
    static bool isSupportedDatasetEntry(const std::filesystem::directory_entry& entry);
    static bool isRegularOrDirectory(const std::filesystem::path& path);
    static std::string lower(std::string value);
    static std::string trim(const std::string& value);
};

} // namespace gisqc
