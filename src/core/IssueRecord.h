#pragma once

#include <string>

namespace gisqc {

enum class Severity {
    Info,
    Warning,
    Error
};

struct IssueRecord {
    std::string issueId;
    std::string ruleCode;
    std::string layerName;
    std::string featureId;
    std::string description;
    Severity severity{Severity::Info};
    std::string status{"待处理"};
    std::string sourcePath;
    std::string geometryWkt;
    double previewX{0.0};
    double previewY{0.0};
    bool hasPreviewGeometry{false};
};

inline const char* toString(Severity severity) {
    switch (severity) {
    case Severity::Warning:
        return "\xe8\xad\xa6\xe5\x91\x8a";
    case Severity::Error:
        return "\xe9\x94\x99\xe8\xaf\xaf";
    default:
        return "\xe6\x8f\x90\xe7\xa4\xba";
    }
}

} // namespace gisqc
