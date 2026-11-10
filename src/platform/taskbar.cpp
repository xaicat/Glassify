#include "platform/taskbar.hpp"
#include "core/config.hpp"
#include <algorithm>
#include <cstdio>

namespace wfx {
std::optional<Window> inspect_taskbar(HWND h) {
    wchar_t cls[128]{};
    GetClassNameW(h, cls, 128);
    if (wcscmp(cls, L"Shell_TrayWnd") && wcscmp(cls, L"Shell_SecondaryTrayWnd"))
        return {};
    if (GetAncestor(h, GA_ROOT) != h || !IsWindowVisible(h))
        return {};
    const auto id = identify(h);
    DWORD shell_pid{};
    GetWindowThreadProcessId(GetShellWindow(), &shell_pid);
    if (!id || !shell_pid || id->pid != shell_pid)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id->pid);
    if (!process)
        return {};
    wchar_t path[32768]{};
    DWORD size = 32768;
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    if (!ok || _wcsicmp(std::filesystem::path(path).filename().c_str(), L"explorer.exe"))
        return {};
    return Window{*id, {utf8(path), utf8(cls), ""}, L"Windows taskbar"};
}
namespace {
std::filesystem::path engine_path() {
    return data_directory() / "TaskbarEngine" / "TranslucentTB.exe";
}
unsigned long long creation_time(HANDLE process) {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process, &c, &e, &k, &u))
        return 0;
    return (static_cast<unsigned long long>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}
} // namespace
bool close_taskbar_companion(const CompanionRecord &r) {
    if (!r.pid || !r.created)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, r.pid);
    if (!process)
        return false;
    wchar_t path[32768]{};
    DWORD count = 32768;
    const bool owned = creation_time(process) == r.created &&
                       QueryFullProcessImageNameW(process, 0, path, &count) &&
                       _wcsicmp(path, engine_path().c_str()) == 0;
    struct Context {
        DWORD pid;
        bool posted = false;
    } context{r.pid};
    if (owned) {
        EnumWindows(
            [](HWND h, LPARAM l) -> BOOL {
                auto &context = *reinterpret_cast<Context *>(l);
                DWORD pid{};
                GetWindowThreadProcessId(h, &pid);
                wchar_t cls[128]{};
                GetClassNameW(h, cls, 128);
                if (pid == context.pid && wcscmp(cls, L"TrayWindow") == 0)
                    context.posted = PostMessageW(h, WM_CLOSE, 0, 0) != FALSE;
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&context));
    }
    CloseHandle(process);
    return owned && context.posted;
}
bool Taskbar::installed() {
    const auto root = engine_path().parent_path();
    for (const auto file : {"TranslucentTB.exe", "ExplorerHooks.dll", "ExplorerTAP.dll", "ProgramLog.dll",
                            "Xaml.dll", "resources.pri", "engine-version.txt"})
        if (!std::filesystem::exists(root / file))
            return false;
    return read_file(root / "engine-version.txt").starts_with("2026.2");
}
void Taskbar::poll() {
    if (!process)
        return;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) {
        CloseHandle(process);
        process = nullptr;
        companion = {};
        recovery.clear_taskbar_companion();
        applied_opacity = -1;
        if (!stopping)
            failed = true;
        stopping = false;
    } else if (stopping)
        close_taskbar_companion(companion);
}
std::string Taskbar::apply(int opacity) {
    poll();
    if (stopping)
        return "Restoring the previous taskbar session. The new setting will apply shortly.";
    if (failed)
        return "The taskbar engine stopped. Turn it off before retrying; your Windows build may be "
               "unsupported.";
    if (!installed())
        return "Set up the taskbar engine to get started.";
    if (!recovery.alive())
        return "Recovery is unavailable. Taskbar has not been changed.";
    if (process && opacity == applied_opacity)
        return status;
    if (!process) {
        HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, L"344635E9-9AE4-4E60-B128-D53E25AB70A7");
        if (existing) {
            CloseHandle(existing);
            return "Another taskbar engine is running. Close it before using Glassify taskbar "
                   "controls.";
        }
    }
    Json::Value config;
    const auto alpha = (std::clamp(opacity, 0, 100) * 255 + 50) / 100;
    char color[16]{};
    snprintf(color, sizeof(color), "#101114%02X", alpha);
    config["desktop_appearance"]["accent"] = "clear";
    config["desktop_appearance"]["color"] = color;
    config["desktop_appearance"]["show_peek"] = true;
    config["desktop_appearance"]["show_line"] = false;
    for (const auto name :
         {"visible_window_appearance", "maximized_window_appearance", "start_opened_appearance",
          "search_opened_appearance", "task_view_opened_appearance", "battery_saver_appearance"})
        config[name]["enabled"] = false;
    config["hide_tray"] = true;
    config["disable_saving"] = true;
    config["verbosity"] = "warn";
    write_atomic(engine_path().parent_path() / "settings.json", stringify(config));
    if (!process) {
        const auto program = engine_path();
        auto command = L"\"" + program.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                            nullptr, program.parent_path().c_str(), &startup, &child))
            return "Could not start the optional taskbar engine.";
        companion = {child.dwProcessId, creation_time(child.hProcess)};
        if (!recovery.watch_taskbar_companion(companion)) {
            TerminateProcess(child.hProcess, 1); // It is still suspended and has not changed Explorer.
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            companion = {};
            return "Could not register taskbar recovery; no changes made.";
        }
        process = child.hProcess;
        ResumeThread(child.hThread);
        CloseHandle(child.hThread);
    }
    applied_opacity = opacity;
    status = "Background opacity set to " + std::to_string(opacity) +
             "%. Icons and labels remain solid.";
    return status;
}
void Taskbar::restore() {
    failed = false;
    if (process) {
        stopping = true;
        close_taskbar_companion(companion);
    }
    applied_opacity = -1;
}
} // namespace wfx
