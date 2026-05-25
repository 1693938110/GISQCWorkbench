#include "../core/LicenseManager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
std::string wideToUtf8(const wchar_t* value) {
    if (!value) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}
#endif

void printUsage() {
    std::cout << "LicenseGenerator - GISQCWorkbench license generator\n"
              << "Usage:\n"
              << "  LicenseGenerator.exe --machine-code\n"
              << "  LicenseGenerator.exe --generate <machine-code> <expire-date YYYY-MM-DD> <max-runs> [output-license.dat]\n\n"
              << "Examples:\n"
              << "  LicenseGenerator.exe --machine-code\n"
              << "  LicenseGenerator.exe --generate ABCD-1234-EF56-7890 2026-12-31 100 license.dat\n";
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("无法写入授权文件：" + path.u8string());
    out << text;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    try {
#ifdef _WIN32
        auto arg = [&](int i) { return wideToUtf8(argv[i]); };
#else
        auto arg = [&](int i) { return std::string(argv[i]); };
#endif
        if (argc < 2) {
            printUsage();
            return 0;
        }

        const auto command = arg(1);
        if (command == "--machine-code") {
            std::cout << "Machine code: " << gisqc::LicenseManager::machineCode() << '\n'
                      << "Default license path: " << gisqc::LicenseManager::defaultLicensePath().u8string() << '\n';
            return 0;
        }

        if (command == "--generate") {
            if (argc < 5) {
                printUsage();
                return 1;
            }
            const auto machineCode = arg(2);
            const auto expireDate = arg(3);
            const int maxRuns = std::stoi(arg(4));
            const auto license = gisqc::LicenseManager::generateLicenseText(machineCode, expireDate, maxRuns);
            const std::filesystem::path output = argc >= 6 ? std::filesystem::u8path(arg(5)) : gisqc::LicenseManager::defaultLicensePath();
            writeFile(output, license);
            std::cout << "License generated: " << output.u8string() << '\n'
                      << "Machine code: " << machineCode << '\n'
                      << "Expire date: " << expireDate << '\n'
                      << "Max runs: " << maxRuns << '\n';
            return 0;
        }

        printUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Failed: " << ex.what() << '\n';
        return 1;
    }
}
