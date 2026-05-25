#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string readText(const std::string& path) {
    std::ifstream input(path);
    assert(input && "expected source file to exist");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

static void assertContains(const std::string& text, const std::string& needle) {
    assert(text.find(needle) != std::string::npos && "expected license registration marker missing");
}

static void assertNotContains(const std::string& text, const std::string& needle) {
    assert(text.find(needle) == std::string::npos && "unexpected old license failure marker found");
}

int main() {
    const auto appMain = readText("src/app/main.cpp");
    const auto installerScript = readText("scripts/build_windows_release.ps1");

    assertContains(appMain, "软件注册");
    assertContains(appMain, "复制机器码");
    assertContains(appMain, "注册并启动");
    assertContains(appMain, "installLicenseText");
    assertContains(appMain, "QApplication::clipboard()");
    assertContains(appMain, "consumeRun");
    assertNotContains(appMain, "请使用 LicenseGenerator.exe 生成 license.dat 后重新启动");

    assertContains(installerScript, "Remove-Item (Join-Path $DeployDir \"license.dat\")");

    return 0;
}
