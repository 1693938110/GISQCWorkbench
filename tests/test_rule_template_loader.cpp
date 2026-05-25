#include "../src/core/RuleTemplateLoader.h"
#include "../src/core/RuleTemplateStore.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

static void testLoadsDefaultRulesTemplate() {
    gisqc::RuleTemplateLoader loader;
    auto loaded = loader.loadFromFile("data/templates/default_rules.json");

    assert(loaded.templateCode == "GIS_QC_STANDARD_V2");
    assert(loaded.templateName == "GIS数据通用标准化质量检查 V2.0");
    assert(loaded.rules.size() >= 70);
    assert(loaded.findRule("A010104") != nullptr);
    assert(loaded.findRule("A010303") != nullptr);
    assert(loaded.findRule("B010202") != nullptr);
    assert(loaded.findRule("C030312") != nullptr);
}

static void testFindsSpatialRuleParameters() {
    gisqc::RuleTemplateLoader loader;
    auto loaded = loader.loadFromFile("data/templates/default_rules.json");
    const auto* rule = loaded.findRule("C020201");

    assert(rule != nullptr);
    assert(rule->name.find("碎线") != std::string::npos);
    assert(rule->targetObject == "线");
    assert(rule->enabled == true);
    assert(rule->severity == "warning");
    assert(rule->parameters.at("tolerance") == "0.001");
    assert(rule->parameters.at("unit") == "meter");
}

static void testSerializesInternalTemplateConfig() {
    gisqc::RuleTemplate templ;
    templ.templateCode = "USER_TEMPLATE";
    templ.templateName = "内部配置测试";

    gisqc::RuleDefinition rule;
    rule.code = "A010103";
    rule.name = "必选目录或文件不得缺失";
    rule.targetObject = "成果目录";
    rule.enabled = false;
    rule.severity = "warning";
    rule.parameters["requiredPaths"] = "文档资料,空间数据";
    rule.message = "缺失必选目录或文件";
    templ.rules.push_back(rule);

    const auto json = gisqc::RuleTemplateStore::toJson(templ);
    gisqc::RuleTemplateLoader loader;
    const auto loaded = loader.loadFromString(json);
    const auto* loadedRule = loaded.findRule("A010103");

    assert(loaded.templateCode == "USER_TEMPLATE");
    assert(loadedRule != nullptr);
    assert(loadedRule->enabled == false);
    assert(loadedRule->severity == "warning");
    assert(loadedRule->parameters.at("requiredPaths") == "文档资料,空间数据");
}

int main() {
    testLoadsDefaultRulesTemplate();
    testFindsSpatialRuleParameters();
    testSerializesInternalTemplateConfig();
    std::cout << "RuleTemplateLoader tests passed\n";
    return 0;
}
