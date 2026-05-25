#pragma once

#include "RuleTemplateLoader.h"

#include <string>

namespace gisqc {

class RuleTemplateStore {
public:
    RuleTemplate loadActive() const;
    RuleTemplate loadDefault() const;
    void saveUserTemplate(const RuleTemplate& templ) const;
    bool resetUserTemplate() const;

    std::string activeTemplatePath() const;
    std::string defaultTemplatePath() const;
    std::string userTemplatePath() const;

    static std::string toJson(const RuleTemplate& templ);
    static void saveToFile(const RuleTemplate& templ, const std::string& path);

private:
    static std::string firstExistingPath(const std::string& fileName);
    static std::string escapeJson(const std::string& value);
    static std::string categoryCodeForRule(const RuleDefinition& rule);
    static std::string categoryName(const std::string& categoryCode);
};

} // namespace gisqc
