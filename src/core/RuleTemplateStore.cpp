#include "RuleTemplateStore.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace gisqc {

namespace fs = std::filesystem;

RuleTemplate RuleTemplateStore::loadActive() const {
    RuleTemplateLoader loader;
    const auto userPath = userTemplatePath();
    if (!userPath.empty() && fs::exists(userPath)) {
        return loader.loadFromFile(userPath);
    }
    return loadDefault();
}

RuleTemplate RuleTemplateStore::loadDefault() const {
    RuleTemplateLoader loader;
    return loader.loadFromFile(defaultTemplatePath());
}

void RuleTemplateStore::saveUserTemplate(const RuleTemplate& templ) const {
    saveToFile(templ, userTemplatePath());
}

bool RuleTemplateStore::resetUserTemplate() const {
    const auto path = userTemplatePath();
    if (path.empty() || !fs::exists(path)) {
        return false;
    }
    return fs::remove(path);
}

std::string RuleTemplateStore::activeTemplatePath() const {
    const auto userPath = userTemplatePath();
    if (!userPath.empty() && fs::exists(userPath)) {
        return userPath;
    }
    return defaultTemplatePath();
}

std::string RuleTemplateStore::defaultTemplatePath() const {
    return firstExistingPath("default_rules.json");
}

std::string RuleTemplateStore::userTemplatePath() const {
    const fs::path current = fs::current_path() / "data" / "templates" / "user_rules.json";
    return current.string();
}

std::string RuleTemplateStore::toJson(const RuleTemplate& templ) {
    std::map<std::string, std::vector<const RuleDefinition*>> grouped;
    for (const auto& rule : templ.rules) {
        grouped[categoryCodeForRule(rule)].push_back(&rule);
    }

    std::ostringstream out;
    out << "{\n";
    out << "  \"templateCode\": \"" << escapeJson(templ.templateCode.empty() ? "GIS_QC_INTERNAL_USER" : templ.templateCode) << "\",\n";
    out << "  \"templateName\": \"" << escapeJson(templ.templateName.empty() ? "GIS数据质量检查内部配置" : templ.templateName) << "\",\n";
    out << "  \"globalTolerance\": \"" << escapeJson(templ.globalTolerance.empty() ? "0.001" : templ.globalTolerance) << "\",\n";
    out << "  \"dataSourcePath\": \"" << escapeJson(templ.dataSourcePath) << "\",\n";
    out << "  \"source\": \"软件内部规则配置\",\n";
    out << "  \"categories\": [\n";

    bool firstCategory = true;
    for (const auto& categoryCode : {"A", "B", "C"}) {
        const auto found = grouped.find(categoryCode);
        if (found == grouped.end() || found->second.empty()) {
            continue;
        }
        if (!firstCategory) {
            out << ",\n";
        }
        firstCategory = false;
        out << "    {\n";
        out << "      \"code\": \"" << categoryCode << "\",\n";
        out << "      \"name\": \"" << escapeJson(categoryName(categoryCode)) << "\",\n";
        out << "      \"rules\": [\n";

        for (std::size_t i = 0; i < found->second.size(); ++i) {
            const auto& rule = *found->second[i];
            out << "        {\n";
            out << "          \"code\": \"" << escapeJson(rule.code) << "\",\n";
            out << "          \"name\": \"" << escapeJson(rule.name) << "\",\n";
            out << "          \"object\": \"" << escapeJson(rule.targetObject) << "\",\n";
            out << "          \"enabled\": " << (rule.enabled ? "true" : "false") << ",\n";
            out << "          \"severity\": \"" << escapeJson(rule.severity.empty() ? "warning" : rule.severity) << "\",\n";
            out << "          \"parameters\": {";
            bool firstParam = true;
            for (const auto& [key, value] : rule.parameters) {
                if (!firstParam) {
                    out << ", ";
                }
                firstParam = false;
                out << "\"" << escapeJson(key) << "\": \"" << escapeJson(value) << "\"";
            }
            out << "},\n";
            out << "          \"message\": \"" << escapeJson(rule.message) << "\"\n";
            out << "        }";
            if (i + 1 < found->second.size()) {
                out << ",";
            }
            out << "\n";
        }

        out << "      ]\n";
        out << "    }";
    }

    out << "\n  ]\n";
    out << "}\n";
    return out.str();
}

void RuleTemplateStore::saveToFile(const RuleTemplate& templ, const std::string& path) {
    const fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path());
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("无法写入内部规则配置：" + path);
    }
    out << toJson(templ);
}

std::string RuleTemplateStore::firstExistingPath(const std::string& fileName) {
    for (const auto& candidate : {
             fs::path("data") / "templates" / fileName,
             fs::path("..") / "data" / "templates" / fileName,
             fs::path("..") / ".." / "data" / "templates" / fileName}) {
        if (fs::exists(candidate)) {
            return candidate.string();
        }
    }
    return (fs::path("data") / "templates" / fileName).string();
}

std::string RuleTemplateStore::escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string RuleTemplateStore::categoryCodeForRule(const RuleDefinition& rule) {
    if (!rule.category.empty()) {
        return rule.category.substr(0, 1);
    }
    if (!rule.code.empty()) {
        return rule.code.substr(0, 1);
    }
    return "C";
}

std::string RuleTemplateStore::categoryName(const std::string& categoryCode) {
    if (categoryCode == "A") {
        return "成果完整性";
    }
    if (categoryCode == "B") {
        return "属性专题";
    }
    return "空间专题";
}

} // namespace gisqc
