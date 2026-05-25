#include "../core/IssueCsvExporter.h"
#include "../core/LicenseManager.h"
#include "../core/RuleTemplateStore.h"
#include "../core/TaskSession.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr int IdInputEdit = 1001;
constexpr int IdBrowseButton = 1002;
constexpr int IdOutputEdit = 1003;
constexpr int IdRunButton = 1004;
constexpr int IdResultEdit = 1005;

HWND g_inputEdit{};
HWND g_outputEdit{};
HWND g_resultEdit{};
HFONT g_font{};

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    }
    return result;
}

std::filesystem::path pathFromUtf8(const std::string& text) {
    const auto wide = utf8ToWide(text);
    return std::filesystem::path(wide);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    return wideToUtf8(path.wstring());
}

std::wstring getWindowText(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    GetWindowTextW(hwnd, text.data(), length + 1);
    return text;
}

std::wstring defaultOutputPath() {
    const auto path = std::filesystem::current_path() / L"GISQCWorkbenchPreview_issues.csv";
    return path.wstring();
}

void setResult(const std::wstring& text) {
    SetWindowTextW(g_resultEdit, text.c_str());
}

void appendControl(HWND parent, HWND child) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    ShowWindow(child, SW_SHOW);
    (void)parent;
}

HMENU controlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

void chooseFolder(HWND parent) {
    BROWSEINFOW browse{};
    browse.hwndOwner = parent;
    browse.lpszTitle = L"选择待检成果目录";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (!pidl) {
        return;
    }

    wchar_t path[MAX_PATH]{};
    if (SHGetPathFromIDListW(pidl, path)) {
        SetWindowTextW(g_inputEdit, path);
        const auto output = std::filesystem::path(path) / L"GISQCWorkbench_问题清单.csv";
        SetWindowTextW(g_outputEdit, output.wstring().c_str());
    }
    CoTaskMemFree(pidl);
}

std::wstring severityText(gisqc::Severity severity) {
    switch (severity) {
    case gisqc::Severity::Error:
        return L"错误";
    case gisqc::Severity::Warning:
        return L"警告";
    default:
        return L"提示";
    }
}

void runCheck(HWND parent) {
    const auto inputWide = getWindowText(g_inputEdit);
    const auto outputWide = getWindowText(g_outputEdit);
    if (inputWide.empty()) {
        MessageBoxW(parent, L"请先选择待检成果目录。", L"缺少输入", MB_ICONWARNING);
        return;
    }
    if (outputWide.empty()) {
        MessageBoxW(parent, L"请填写问题清单 CSV 输出路径。", L"缺少输出", MB_ICONWARNING);
        return;
    }

    EnableWindow(GetDlgItem(parent, IdRunButton), FALSE);
    setResult(L"正在执行质检，请稍候...");

    try {
        gisqc::RuleTemplateStore store;
        const auto templ = store.loadActive();

        gisqc::TaskSession session;
        const auto report = session.run("Windows预览质检任务", wideToUtf8(inputWide), templ.rules);

        std::ofstream out(pathFromUtf8(wideToUtf8(outputWide)), std::ios::binary | std::ios::trunc);
        const std::string bom = "\xEF\xBB\xBF";
        out << bom << gisqc::IssueCsvExporter::toCsv(report.issues);

        std::wostringstream view;
        view << L"任务：" << utf8ToWide(report.task.name()) << L"\r\n"
             << L"成果目录：" << inputWide << L"\r\n"
             << L"数据源数量：" << report.scan.sourceCount << L"\r\n"
             << L"规则数量：" << report.statistics.totalRules << L"\r\n"
             << L"规则配置：" << utf8ToWide(store.activeTemplatePath()) << L"\r\n"
             << L"问题数量：" << report.issues.size() << L"\r\n"
             << L"通过率：" << utf8ToWide(report.statistics.passRateText) << L"\r\n"
             << L"CSV：" << outputWide << L"\r\n\r\n"
             << L"问题清单预览\r\n";

        const std::size_t previewCount = std::min<std::size_t>(report.issues.size(), 300);
        for (std::size_t i = 0; i < previewCount; ++i) {
            const auto& issue = report.issues[i];
            view << utf8ToWide(issue.issueId) << L"  "
                 << utf8ToWide(issue.ruleCode) << L"  "
                 << severityText(issue.severity) << L"  "
                 << utf8ToWide(issue.layerName) << L" / "
                 << utf8ToWide(issue.featureId) << L"\r\n"
                 << L"    " << utf8ToWide(issue.description) << L"\r\n";
        }
        if (report.issues.size() > previewCount) {
            view << L"\r\n仅显示前 " << previewCount << L" 条，完整结果请查看 CSV。";
        }

        setResult(view.str());
    } catch (const std::exception& ex) {
        const auto message = L"执行失败：\r\n" + utf8ToWide(ex.what());
        setResult(message);
        MessageBoxW(parent, message.c_str(), L"执行失败", MB_ICONERROR);
    }

    EnableWindow(GetDlgItem(parent, IdRunButton), TRUE);
}

void layoutControls(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int margin = 14;
    const int labelWidth = 86;
    const int buttonWidth = 90;
    const int rowHeight = 28;
    const int editLeft = margin + labelWidth;
    const int editWidth = width - editLeft - buttonWidth - margin * 2 - 8;

    MoveWindow(GetDlgItem(hwnd, 2001), margin, margin + 4, labelWidth, rowHeight, TRUE);
    MoveWindow(g_inputEdit, editLeft, margin, editWidth, rowHeight, TRUE);
    MoveWindow(GetDlgItem(hwnd, IdBrowseButton), editLeft + editWidth + 8, margin, buttonWidth, rowHeight, TRUE);

    MoveWindow(GetDlgItem(hwnd, 2002), margin, margin + 42, labelWidth, rowHeight, TRUE);
    MoveWindow(g_outputEdit, editLeft, margin + 38, width - editLeft - buttonWidth - margin * 2 - 8, rowHeight, TRUE);
    MoveWindow(GetDlgItem(hwnd, IdRunButton), width - buttonWidth - margin, margin + 38, buttonWidth, rowHeight, TRUE);

    MoveWindow(g_resultEdit, margin, margin + 80, width - margin * 2, rc.bottom - margin - 80, TRUE);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

        appendControl(hwnd, CreateWindowW(L"STATIC", L"成果目录", WS_CHILD, 0, 0, 0, 0, hwnd, controlId(2001), nullptr, nullptr));
        g_inputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, controlId(IdInputEdit), nullptr, nullptr);
        appendControl(hwnd, g_inputEdit);
        appendControl(hwnd, CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_TABSTOP,
            0, 0, 0, 0, hwnd, controlId(IdBrowseButton), nullptr, nullptr));

        appendControl(hwnd, CreateWindowW(L"STATIC", L"输出CSV", WS_CHILD, 0, 0, 0, 0, hwnd, controlId(2002), nullptr, nullptr));
        g_outputEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultOutputPath().c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, controlId(IdOutputEdit), nullptr, nullptr);
        appendControl(hwnd, g_outputEdit);
        appendControl(hwnd, CreateWindowW(L"BUTTON", L"开始质检", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,
            0, 0, 0, 0, hwnd, controlId(IdRunButton), nullptr, nullptr));

        g_resultEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
            L"GIS 数据质量检查工作台 - Windows 预览版\r\n\r\n选择成果目录后点击“开始质检”。当前预览版复用 core 规则引擎，可输出 CSV 问题清单；Qt 正式桌面版将在 Qt6 SDK 就绪后构建。",
            WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
            0, 0, 0, 0, hwnd, controlId(IdResultEdit), nullptr, nullptr);
        appendControl(hwnd, g_resultEdit);
        layoutControls(hwnd);
        return 0;
    }
    case WM_SIZE:
        layoutControls(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IdBrowseButton) {
            chooseFolder(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IdRunButton) {
            runCheck(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (g_font) {
            DeleteObject(g_font);
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const auto license = gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                                      gisqc::LicenseManager::defaultStatePath(),
                                                      true);
    if (!license.valid) {
        const auto message = utf8ToWide(license.message + "\n\n机器码：" + gisqc::LicenseManager::machineCode() +
            "\n授权文件位置：" + gisqc::LicenseManager::defaultLicensePath().u8string() +
            "\n\n请使用 LicenseGenerator.exe 生成 license.dat 后重新启动。");
        MessageBoxW(nullptr, message.c_str(), L"授权校验失败", MB_ICONERROR);
        CoUninitialize();
        return 3;
    }

    const wchar_t className[] = L"GISQCWorkbenchWinPreview";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, className, L"GIS 数据质量检查工作台 - 预览版",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720,
        nullptr, nullptr, instance, nullptr);

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
