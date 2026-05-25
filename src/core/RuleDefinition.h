#pragma once

#include <map>
#include <string>

namespace gisqc {

struct RuleDefinition {
    std::string code;
    std::string name;
    std::string category;
    std::string targetObject;
    bool enabled{true};
    std::string severity{"error"};
    std::map<std::string, std::string> parameters;
    std::string message;
};

} // namespace gisqc
