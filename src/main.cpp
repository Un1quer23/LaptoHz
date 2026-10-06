#include "app.hpp"
#include <shellapi.h>
#include <vector>

namespace {
HWND RunningHost() {
    HWND host = FindWindowW(rrs::kWindowClass, rrs::kAppName);
    // Match only the old or new production caption, excluding preview windows.
    return host ? host : FindWindowW(rrs::kWindowClass, L"Refresh Rate Switcher");
}
void Output(const std::string& text) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(output, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
}
void StartupFailure(const std::wstring& text) noexcept {
    try { rrs::Logger(rrs::DataDirectory()).Write(L"启动失败 · " + text); } catch (...) {}
    OutputDebugStringW(text.c_str());
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace rrs;
    HANDLE singleton = nullptr;
    try {
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) return 2;
        std::vector<std::wstring> args(arguments + 1, arguments + count);
        LocalFree(arguments);
        AppOptions options;
        bool diagnose = false;
        std::wstring control;
        std::optional<WPARAM> control_value;
        UINT control_message = kControlMessage;
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& arg = args[i];
            if (arg == L"--startup") options.startup = true;
            else if (arg == L"--no-startup") options.skip_startup_initialization = true;
            else if (arg == L"--diagnose") diagnose = true;
            else if (arg == L"--preview-notification") options.preview = true;
            else if ((arg == L"--output" || arg == L"--capture") && i + 1 < args.size()) {
                if (arg == L"--output") options.report = args[++i]; else options.capture = args[++i];
            } else if (arg == L"--mode" || arg == L"--switch") {
                if (!control.empty() || i + 1 >= args.size()) return 2;
                const auto& value = args[++i];
                if (arg == L"--mode") {
                    const auto mode = ParseMode(value);
                    if (!mode) return 2;
                    control_value = *mode == Mode::manual ? 0 : *mode == Mode::automatic ? 1 : 3;
                } else {
                    const auto hz = ParseRefreshTarget(value);
                    if (!hz || !*hz) return 2;
                    control_value = static_cast<WPARAM>(*hz); control_message = kSwitchMessage;
                }
                control = arg;
            } else if (arg == L"--exit" || arg == L"--pause" || arg == L"--resume" || arg == L"--status") {
                if (!control.empty()) return 2;
                control = arg;
            }
            else if (arg == L"--version") { Output(Utf8(kAppName) + " " + Utf8(kVersion) + "\n"); return 0; }
            else { Output("Invalid argument. See README.md.\n"); return 2; }
        }
        if ((diagnose && options.preview) || (!control.empty() && (diagnose || options.preview))) return 2;
        if (!control.empty()) {
            HWND host = RunningHost();
            if (!host) return 1;
            if (control == L"--exit") return PostMessageW(host, WM_CLOSE, 0, 0) ? 0 : 1;
            const WPARAM state = control_value.value_or(control == L"--pause" ? 0 : control == L"--resume" ? 1 : 2);
            DWORD_PTR reply = 0;
            if (!SendMessageTimeoutW(host,control_message,state,0,SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,3000,&reply)) {
                Output("The running tool did not respond. Retry after it recovers.\n"); return 1;
            }
            switch (static_cast<ControlResult>(reply)) {
            case ControlResult::accepted: return 0;
            case ControlResult::manual_required:
                Output("Enter manual mode first: --mode manual, then --switch <supported integer Hz>.\n"); return 4;
            case ControlResult::busy: Output("A refresh-rate change is already in progress. Retry after it completes.\n"); return 4;
            case ControlResult::unavailable: Output("The requested operation is currently unavailable.\n"); return 4;
            case ControlResult::not_saved: Output("The selected mode is active, but settings could not be saved.\n"); return 5;
            case ControlResult::invalid: Output("The running tool rejected this control command.\n"); return 4;
            }
            Output("Restart the tool to load the current version before sending commands.\n"); return 1;
        }
        if (diagnose) {
            DisplayBackend backend;
            const auto snapshot = backend.Inspect();
            const auto json = DiagnosticJson(snapshot,backend,LoadRefreshTargets(DataDirectory()));
            if (!options.report.empty() && !WriteText(options.report, json)) return 3;
            Output(json);
            return snapshot.policy.availability == Availability::error ? 1 : 0;
        }
        if (!options.preview) {
            // Share the existing singleton with releases using the old product name.
            singleton = CreateMutexW(nullptr, FALSE, L"Local\\RefreshRateSwitcher.Instance.v1");
            if (!singleton) { StartupFailure(L"无法创建单实例标记 · " + NativeError(GetLastError())); return 2; }
            if (GetLastError() == ERROR_ALREADY_EXISTS) {
                HWND host = RunningHost();
                if (host) PostMessageW(host, WM_APP + 10, 2, 0);
                else StartupFailure(L"已有单实例标记，但主窗口尚未就绪");
                CloseHandle(singleton); return host ? 0 : 1;
            }
        }
        App app;
        const int result = app.Run(instance, options);
        if (singleton) CloseHandle(singleton);
        if (result && !options.preview) StartupFailure(L"运行初始化或消息循环返回 " + std::to_wstring(result));
        return result;
    } catch (const std::exception& error) {
        if (singleton) CloseHandle(singleton);
        Output(std::string("Startup failed: ") + error.what() + "\n");
        const std::string detail = error.what(); StartupFailure(std::wstring(detail.begin(),detail.end()));
        return 2;
    }
}
