#pragma once

#include "RuleDefinition.h"

#include <string>
#include <vector>

namespace gisqc {

struct RuleTemplate {
    std::string templateCode;
    std::string templateName;
    std::string globalTolerance{"0.001"};
    std::string dataSourcePath;
    std::vector<RuleDefinition> rules;

    const RuleDefinition* findRule(const std::string& code) const;
};

class RuleTemplateLoader {
public:
    RuleTemplate loadFromFile(const std::string& path) const;
    RuleTemplate loadFromString(const std::string& jsonText) const;

private:
    static std::string extractStringField(const std::string& objectText, const std::string& fieldName);
    static bool extractBoolField(const std::string& objectText, const std::string& fieldName, bool defaultValue);
    static std::map<std::string, std::string> extractParameters(const std::string& objectText);
};

} // namespace gisqc
