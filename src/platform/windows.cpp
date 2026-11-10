#include "platform/windows.hpp"
#include <algorithm>
#include <dwmapi.h>
#include <fstream>
#include <shlobj.h>
#include <stdexcept>

namespace wfx {
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    const auto size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!size)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), result.data(),
                        size);
    return result;
}
std::string utf8(const std::wstring &s) {
    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), size, nullptr,
                        nullptr);
    return result;
}
std::optional<Identity> identify(HWND hwnd) {
    DWORD pid{};
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};
    FILETIME created{}, exit{}, kernel{}, user{};
    const bool ok = GetProcessTimes(process, &created, &exit, &kernel, &user) != FALSE;
    CloseHandle(process);
    if (!ok)
        return {};
    return Identity{hwnd, pid,
                    (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime};
}
bool same_identity(const Identity &id) {
    const auto now = identify(id.hwnd);
    return now && now->pid == id.pid && now->created == id.created;
}
std::string exclusion(HWND hwnd, bool include_own, bool allow_fullscreen) {
    if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd)
        return "Not a top-level window";
    if (!IsWindowVisible(hwnd))
        return "Window is hidden";
    if (IsHungAppWindow(hwnd))
        return "App is not responding";
    DWORD pid{};
    GetWindowThreadProcessId(hwnd, &pid);
    if (!include_own && pid == GetCurrentProcessId())
        return "Own window";
    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    for (auto name : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"#32768",
                      L"tooltips_class32"})
        if (_wcsicmp(cls, name) == 0)
            return "Shell or transient surface";
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
        return "Tool window";
    DWORD cloaked{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return "Cloaked window";
    const auto style = GetClassLongPtrW(hwnd, GCL_STYLE);
    if (style & (CS_OWNDC | CS_CLASSDC))
        return "Private/class DC rendering is excluded";
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return "Process identity unavailable";
    HANDLE token{};
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    bool elevated = true;
    if (OpenProcessToken(process, TOKEN_QUERY, &token)) {
        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    CloseHandle(process);
    if (elevated && !include_own)
        return "Elevated or inaccessible process";
    RECT rect{}, monitor{};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!allow_fullscreen && !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION) &&
        GetWindowRect(hwnd, &rect) &&
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        monitor = mi.rcMonitor;
        if (rect.left <= monitor.left && rect.top <= monitor.top && rect.right >= monitor.right &&
            rect.bottom >= monitor.bottom)
            return "Fullscreen borderless window";
    }
    return {};
}
std::optional<Window> inspect(HWND hwnd, bool include_own, bool allow_fullscreen) {
    if (!exclusion(hwnd, include_own, allow_fullscreen).empty())
        return {};
    auto id = identify(hwnd);
    if (!id)
        return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id->pid);
    if (!process)
        return {};
    wchar_t path[32768]{};
    DWORD length = 32768;
    const auto ok = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!ok)
        return {};
    const auto filename = std::filesystem::path(path).filename().wstring();
    for (const auto name :
         {L"StartMenuExperienceHost.exe", L"ShellExperienceHost.exe", L"SearchHost.exe", L"ShellHost.exe"})
        if (_wcsicmp(filename.c_str(), name) == 0)
            return {};
    wchar_t title[1024]{}, cls[256]{};
    GetWindowTextW(hwnd, title, 1024);
    GetClassNameW(hwnd, cls, 256);
    return Window{*id, {utf8(path), utf8(cls), utf8(title)}, title};
}
bool focus_window(HWND hwnd) {
    return IsZoomed(hwnd) || fullscreen_window(hwnd);
}
bool fullscreen_window(HWND hwnd) {
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CAPTION)
        return false;
    RECT r{};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    return GetWindowRect(hwnd, &r) &&
           GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) &&
           r.left <= mi.rcMonitor.left && r.top <= mi.rcMonitor.top && r.right >= mi.rcMonitor.right &&
           r.bottom >= mi.rcMonitor.bottom;
}
std::vector<Window> enumerate() {
    std::vector<Window> result;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            if (auto w = inspect(hwnd))
                reinterpret_cast<std::vector<Window> *>(param)->push_back(std::move(*w));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}
std::optional<EffectState> capture(HWND hwnd, Mode mode) {
    if (mode != Mode::opacity)
        return {};
    EffectState state;
    if (mode == Mode::opacity) {
        state.layered = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) != 0;
        if (state.layered && !GetLayeredWindowAttributes(hwnd, &state.color, &state.alpha, &state.flags))
            return {};
    }
    return state;
}
EffectState desired(const EffectState &state, Mode mode, int opacity) {
    auto result = state;
    if (mode == Mode::opacity) {
        result.layered = true;
        result.alpha = static_cast<BYTE>((opacity * 255 + 50) / 100);
        result.flags |= LWA_ALPHA;
    }
    return result;
}
bool state_matches(const EffectState &a, const EffectState &b, Mode mode) {
    return mode == Mode::opacity
               ? (a.layered == b.layered &&
                  (!a.layered || (a.alpha == b.alpha && a.color == b.color && a.flags == b.flags)))
               : a.backdrop == b.backdrop;
}
bool set_effect(HWND hwnd, Mode mode, const EffectState &state) {
    if (mode != Mode::opacity)
        return false;
    const auto current = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const auto next =
        state.layered ? current | WS_EX_LAYERED : current & ~static_cast<LONG_PTR>(WS_EX_LAYERED);
    if (current != next) {
        SetLastError(0);
        if (!SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next) && GetLastError())
            return false;
    }
    if (state.layered && !SetLayeredWindowAttributes(hwnd, state.color, state.alpha, state.flags))
        return false;
    if (!state.layered)
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    return true;
}
bool restore_record(const Record &r) {
    if (!r.used || !same_identity(r.identity) ||
        reinterpret_cast<ULONG_PTR>(GetPropW(r.identity.hwnd, owner_property)) != r.cookie)
        return false;
    const auto current = capture(r.identity.hwnd, r.mode);
    const bool owns =
        current && (state_matches(*current, r.after, r.mode) || state_matches(*current, r.before, r.mode));
    const bool ok = owns && set_effect(r.identity.hwnd, r.mode, r.original);
    RemovePropW(r.identity.hwnd, owner_property);
    return ok;
}
bool restore_minimized_record(const MinimizedRecord &r) {
    if (!r.used || !same_identity(r.identity) ||
        reinterpret_cast<ULONG_PTR>(GetPropW(r.identity.hwnd, focus_property)) != r.cookie)
        return false;
    bool restored = false;
    if (IsIconic(r.identity.hwnd)) {
        auto placement = r.placement;
        placement.showCmd = placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE;
        restored = SetWindowPlacement(r.identity.hwnd, &placement) != FALSE;
    }
    RemovePropW(r.identity.hwnd, focus_property);
    return restored;
}
std::filesystem::path data_directory() {
    wchar_t override_path[32768]{};
    const auto length = GetEnvironmentVariableW(L"WINFADEX_DATA_DIR", override_path, 32768);
    if (length && length < 32768) {
        std::filesystem::path result(override_path);
        if (!result.is_absolute())
            throw std::runtime_error("WINFADEX_DATA_DIR must be absolute");
        std::filesystem::create_directories(result);
        return result;
    }
    PWSTR path{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
        throw std::runtime_error("LocalAppData unavailable");
    const auto previous = std::filesystem::path(path) / L"VeilDesk" / "config.json";
    const auto legacy = std::filesystem::exists(previous)
                            ? previous
                            : std::filesystem::path(path) / L"WinFadeXNext" / "config.json";
    std::filesystem::path result = std::filesystem::path(path) / L"Glassify";
    CoTaskMemFree(path);
    std::filesystem::create_directories(result);
    if (!std::filesystem::exists(result / "config.json") && std::filesystem::exists(legacy)) {
        std::error_code error;
        std::filesystem::copy_file(legacy, result / "config.json", std::filesystem::copy_options::none,
                                   error);
    }
    return result;
}
std::filesystem::path executable_path() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return path;
}
std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read file");
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0 || size > 4 * 1024 * 1024)
        throw std::runtime_error("File size limit");
    file.seekg(0);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write_atomic(const std::filesystem::path &path, const std::string &data) {
    const auto temp = path.wstring() + L".wfx-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file =
        CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create temporary settings file");
    DWORD written{};
    const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                    written == data.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temp.c_str());
        throw std::runtime_error("Atomic settings replacement failed");
    }
}
void log(const std::string &message) {
    try {
        const auto path = data_directory() / "diagnostics.log";
        if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 256 * 1024) {
            const auto previous = data_directory() / "diagnostics.previous.log";
            std::error_code ec;
            std::filesystem::remove(previous, ec);
            std::filesystem::rename(path, previous, ec);
        }
        std::ofstream(path, std::ios::app) << GetTickCount64() << " " << message << '\n';
    } catch (...) {
    }
}
bool policy_blocks(bool power_saving) {
    HIGHCONTRASTW high{};
    high.cbSize = sizeof(high);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high), &high, 0) &&
        (high.dwFlags & HCF_HIGHCONTRASTON))
        return true;
    // Layered-window opacity is independent of the Fluent transparency preference.
    if (GetSystemMetrics(SM_REMOTESESSION))
        return true;
    SYSTEM_POWER_STATUS power{};
    return power_saving && GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
}
unsigned windows_build() {
    DWORD build{};
    wchar_t value[32]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                     L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS)
        build = static_cast<DWORD>(_wtoi(value));
    return build;
}
} // namespace wfx
