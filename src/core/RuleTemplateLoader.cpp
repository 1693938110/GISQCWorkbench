#include "RuleTemplateLoader.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace gisqc {

const RuleDefinition* RuleTemplate::findRule(const std::string& code) const {
    for (const auto& rule : rules) {
        if (rule.code == code) {
            return &rule;
        }
    }
    return nullptr;
}

RuleTemplate RuleTemplateLoader::loadFromFile(const std::string& path) const {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("无法打开规则模板文件：" + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return loadFromString(buffer.str());
}

RuleTemplate RuleTemplateLoader::loadFromString(const std::string& jsonText) const {
    RuleTemplate templ;
    templ.templateCode = extractStringField(jsonText, "templateCode");
    templ.templateName = extractStringField(jsonText, "templateName");
    const auto gt = extractStringField(jsonText, "globalTolerance");
    if (!gt.empty()) templ.globalTolerance = gt;
    templ.dataSourcePath = extractStringField(jsonText, "dataSourcePath");

    std::regex ruleRegex(R"REGEX(\{[^\{\}]*"code"\s*:\s*"([^"]+)"[^\{\}]*"enabled"\s*:\s*(true|false)[\s\S]*?"parameters"\s*:\s*\{[^\}]*\}[\s\S]*?"message"\s*:\s*"[^"]*"\s*\})REGEX");
    auto begin = std::sregex_iterator(jsonText.begin(), jsonText.end(), ruleRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        const std::string objectText = it->str();
        RuleDefinition rule;
        rule.code = extractStringField(objectText, "code");
        rule.name = extractStringField(objectText, "name");
        rule.targetObject = extractStringField(objectText, "object");
        rule.enabled = extractBoolField(objectText, "enabled", true);
        rule.severity = extractStringField(objectText, "severity");
        rule.message = extractStringField(objectText, "message");
        rule.parameters = extractParameters(objectText);
        templ.rules.push_back(rule);
    }

    return templ;
}

std::string RuleTemplateLoader::extractStringField(const std::string& objectText, const std::string& fieldName) {
    const std::regex pattern("\\\"" + fieldName + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    if (std::regex_search(objectText, match, pattern)) {
        return match[1].str();
    }
    return {};
}

bool RuleTemplateLoader::extractBoolField(const std::string& objectText, const std::string& fieldName, bool defaultValue) {
    const std::regex pattern("\\\"" + fieldName + "\\\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(objectText, match, pattern)) {
        return match[1].str() == "true";
    }
    return defaultValue;
}

std::map<std::string, std::string> RuleTemplateLoader::extractParameters(const std::string& objectText) {
    std::map<std::string, std::string> params;
    const std::regex paramsBlockPattern(R"("parameters"\s*:\s*\{([^\}]*)\})");
    std::smatch blockMatch;
    if (!std::regex_search(objectText, blockMatch, paramsBlockPattern)) {
        return params;
    }

    const std::string block = blockMatch[1].str();
    const std::regex pairPattern(R"REGEX("([^"]+)"\s*:\s*("([^"]*)"|[-+]?[0-9]*\.?[0-9]+|true|false))REGEX");
    auto begin = std::sregex_iterator(block.begin(), block.end(), pairPattern);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        const std::string key = (*it)[1].str();
        std::string value = (*it)[2].str();
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        params[key] = value;
    }
    return params;
}

} // namespace gisqc
