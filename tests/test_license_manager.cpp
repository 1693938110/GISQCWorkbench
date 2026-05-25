#include "../src/core/LicenseManager.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    const auto root = fs::temp_directory_path() / "gis_qc_license_manager_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto licensePath = root / "license.dat";
    const auto statePath = root / "license.state";

    const auto machine = gisqc::LicenseManager::machineCode();
    assert(!machine.empty());

    const auto license = gisqc::LicenseManager::generateLicenseText(machine, "2099-12-31", 2, "2026-05-24");
    assert(gisqc::LicenseManager::installLicenseText(license, licensePath));

    auto status = gisqc::LicenseManager::check(licensePath, statePath, false);
    assert(status.valid);
    assert(status.license.usedRuns == 0);

    status = gisqc::LicenseManager::check(licensePath, statePath, true);
    assert(status.valid);
    assert(status.license.usedRuns == 1);

    status = gisqc::LicenseManager::check(licensePath, statePath, true);
    assert(status.valid);
    assert(status.license.usedRuns == 2);

    status = gisqc::LicenseManager::check(licensePath, statePath, false);
    assert(!status.valid);
    assert(status.runLimitExceeded);

    const auto expired = gisqc::LicenseManager::generateLicenseText(machine, "2000-01-01", 10, "1999-01-01");
    assert(gisqc::LicenseManager::installLicenseText(expired, licensePath));
    fs::remove(statePath);
    status = gisqc::LicenseManager::check(licensePath, statePath, false);
    assert(!status.valid);
    assert(status.expired);

    auto bad = gisqc::LicenseManager::generateLicenseText("0000-0000-0000-0000", "2099-12-31", 10, "2026-05-24");
    assert(gisqc::LicenseManager::installLicenseText(bad, licensePath));
    status = gisqc::LicenseManager::check(licensePath, statePath, false);
    assert(!status.valid);
    assert(status.machineMismatch);

    fs::remove_all(root);
    std::cout << "LicenseManager tests passed\n";
    return 0;
}
