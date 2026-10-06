#include "support.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace rrs;
int main() {
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH,temporary)) return 1;
    const auto directory = std::filesystem::path(temporary) /
        (L"rrs-log-path-中文 空格-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(directory);
    const auto file = directory / L"诊断 日志.log";
    int assertions = 0, result = 0;
    HANDLE writer = INVALID_HANDLE_VALUE;
    const auto check = [&](bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); };
    try {
        const std::string content = Utf8(L"启动记录 · 自动模式\n查看诊断日志\n");
        check(WriteText(file,content),"Create a log with Unicode and spaces in its path.");
        std::filesystem::path resolved = L"stale";
        std::wstring error = L"stale error";
        check(ResolveDiagnosticLogPath(file,resolved,error),"Resolve an existing log to its actual path.");
        check(error.empty() && resolved.is_absolute(),"Success clears the error and returns an absolute path.");
        std::ifstream stream(resolved,std::ios::binary);
        check(stream.good(),"The returned path can independently open the file.");
        const std::string actual{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
        check(actual == content,"Resolving the log preserves its exact UTF-8 content.");
        stream.close();
        std::filesystem::path repeated;
        check(ResolveDiagnosticLogPath(resolved,repeated,error) && repeated == resolved,
            "An already physical path remains usable without further redirection.");
        writer = CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(writer != INVALID_HANDLE_VALUE,"Keep a logging writer open.");
        check(ResolveDiagnosticLogPath(file,repeated,error) && repeated == resolved,
            "An active logging writer does not prevent resolving the path.");
        CloseHandle(writer); writer = INVALID_HANDLE_VALUE;
        check(!ResolveDiagnosticLogPath(directory,resolved,error) && resolved.empty() && !error.empty(),
            "A directory is rejected and clears a previous result.");
        const auto missing = directory / L"missing.log";
        check(!ResolveDiagnosticLogPath(missing,resolved,error) && resolved.empty() && error.find(missing.wstring()) != std::wstring::npos,
            "Missing logs report the requested path without returning stale data.");
        check(!ResolveDiagnosticLogPath(directory / L"missing" / L"switcher.log",resolved,error) && !error.empty(),
            "Missing parent directories are reported.");
        std::cout << "PASS: " << assertions << " diagnostic log path assertions.\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; result = 1; }
    if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
    std::filesystem::remove(file); std::filesystem::remove(directory);
    return result;
}
