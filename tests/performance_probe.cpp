#include "core/config.hpp"
#include "platform/windows.hpp"
#include <dwmapi.h>
#include <iostream>
#include <psapi.h>

namespace {
using namespace wfx;
double cpu(HANDLE h) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(h, &c, &e, &k, &u))
        return 0;
    return (static_cast<double>((static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime) +
            static_cast<double>((static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime)) /
           1e7;
}
void pump(unsigned ms) {
    const auto end = GetTickCount64() + ms;
    do {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    } while (GetTickCount64() < end);
}
} // namespace
int main() {
    using namespace wfx;
    if (FindWindowW(L"WinFadeX.Next", nullptr)) {
        std::cerr << "Quit Glassify first\n";
        return 1;
    }
    const auto scratch = executable_path().parent_path() / "benchmark-data";
    std::filesystem::create_directories(scratch);
    SetEnvironmentVariableW(L"WINFADEX_DATA_DIR", scratch.c_str());
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"Glassify.Benchmark";
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&cls);
    std::vector<HWND> windows;
    for (int i = 0; i < 8; ++i) {
        auto h =
            CreateWindowW(cls.lpszClassName, L"Glassify controlled performance fixture", WS_OVERLAPPEDWINDOW,
                          30 + i * 24, 40 + i * 24, 500, 300, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        windows.push_back(h);
    }
    Json::Value report(Json::objectValue);
    report["windowsBuild"] = windows_build();
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    report["logicalProcessors"] = static_cast<unsigned>(info.dwNumberOfProcessors);
    report["windowCount"] = 8;
    DWORD cloaked{};
    DwmGetWindowAttribute(windows[0], DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    report["fixturesCloaked"] = cloaked != 0;
    report["notes"] =
        "Production controller and mouse hook. Eight static-content windows; one moves at 20 Hz. CPU "
        "excludes recovery helper and DWM. No GPU/frame-time measurement. Not a game or browser benchmark.";
    report["scenarios"] = Json::Value(Json::arrayValue);
    for (int mode = 0; mode < 3; ++mode) {
        Config config;
        if (mode) {
            Rule r;
            r.id = "performance";
            r.executable = utf8(executable_path().wstring());
            r.appearance = Appearance{Mode::opacity, 85, 70, mode == 2 ? 1 : 0, 12};
            config.rules.push_back(r);
        }
        write_atomic(scratch / "config.json", serialize_config(config));
        auto program = executable_path().parent_path() / "Glassify.exe";
        auto command = L"\"" + program.wstring() + L"\" --background";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                            &startup, &child))
            return 2;
        pump(2000);
        HWND controller = FindWindowW(L"WinFadeX.Next", nullptr);
        DWORD pid{};
        GetWindowThreadProcessId(controller, &pid);
        if (pid != child.dwProcessId)
            return 3;
        auto state = capture(windows[0], Mode::opacity);
        if (mode && (!state || !state->layered || (state->alpha != 179 && state->alpha != 217))) {
            PostMessageW(controller, WM_COMMAND, 142, 0);
            return 4;
        }
        const double start_cpu = cpu(child.hProcess);
        const auto start = GetTickCount64();
        SIZE_T peak = 0;
        DWORD handles = 0;
        unsigned moves = 0;
        while (GetTickCount64() - start < 30000) {
            SetWindowPos(windows[0], nullptr, 30 + (moves % 40), 40, 500, 300, SWP_NOZORDER | SWP_NOACTIVATE);
            ++moves;
            pump(50);
            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            if (GetProcessMemoryInfo(child.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                     sizeof(memory)))
                peak = std::max(peak, memory.PrivateUsage);
            GetProcessHandleCount(child.hProcess, &handles);
        }
        const double seconds = (GetTickCount64() - start) / 1000.0;
        Json::Value row;
        row["mode"] = mode == 0 ? "no-effects" : mode == 1 ? "opacity" : "opacity-and-tint";
        row["seconds"] = seconds;
        row["normalizedCpuPercent"] =
            100 * (cpu(child.hProcess) - start_cpu) / seconds / info.dwNumberOfProcessors;
        row["peakControllerPrivateBytes"] = Json::UInt64(peak);
        row["controllerHandles"] = static_cast<unsigned>(handles);
        row["movementEvents"] = moves;
        report["scenarios"].append(row);
        PostMessageW(controller, WM_COMMAND, 142, 0);
        const auto deadline = GetTickCount64() + 10000;
        while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline)
            pump(10);
        if (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT)
            return 5;
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        pump(200);
    }
    for (auto h : windows)
        DestroyWindow(h);
    write_atomic(executable_path().parent_path() / "performance.json", stringify(report));
    std::cout << stringify(report);
}
