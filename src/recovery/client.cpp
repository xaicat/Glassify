#include "recovery/client.hpp"
#include "platform/taskbar.hpp"
#include <algorithm>
#include <new>

namespace wfx {
bool Recovery::lock() {
    if (!mutex)
        return false;
    const auto status = WaitForSingleObject(mutex, 2000);
    return status == WAIT_OBJECT_0 || status == WAIT_ABANDONED;
}
Recovery::~Recovery() {
    if (shared)
        close_taskbar_companion(shared->taskbar_companion);
    restore_minimized();
    restore_all();
    if (shared)
        UnmapViewOfFile(shared);
    if (mapping)
        CloseHandle(mapping);
    if (mutex)
        CloseHandle(mutex);
    if (process)
        CloseHandle(process);
}
bool Recovery::start() {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(SharedState), nullptr);
    mutex = CreateMutexW(&sa, FALSE, nullptr);
    HANDLE ready = CreateEventW(&sa, TRUE, FALSE, nullptr), parent{};
    DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &parent, SYNCHRONIZE, TRUE,
                    0);
    if (!mapping || !mutex || !ready || !parent) {
        if (ready)
            CloseHandle(ready);
        if (parent)
            CloseHandle(parent);
        return false;
    }
    shared =
        static_cast<SharedState *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
    if (!shared) {
        CloseHandle(ready);
        CloseHandle(parent);
        return false;
    }
    new (shared) SharedState{};
    const auto helper = executable_path().parent_path() / L"GlassifyRecovery.exe";
    std::wstring command = L"\"" + helper.wstring() + L"\" " +
                           std::to_wstring(reinterpret_cast<ULONG_PTR>(mapping)) + L" " +
                           std::to_wstring(reinterpret_cast<ULONG_PTR>(mutex)) + L" " +
                           std::to_wstring(reinterpret_cast<ULONG_PTR>(ready)) + L" " +
                           std::to_wstring(reinterpret_cast<ULONG_PTR>(parent));
    // Restrict handle inheritance to the four handles used by this protocol.
    SIZE_T bytes{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    bool initialized = InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE;
    HANDLE handles[]{mapping, mutex, ready, parent};
    bool attributes_ok =
        initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                                 sizeof(handles), nullptr, nullptr);
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION child{};
    bool launched = attributes_ok && CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
                                                    EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr,
                                                    nullptr, &startup.StartupInfo, &child);
    if (initialized)
        DeleteProcThreadAttributeList(attributes);
    if (launched) {
        process = child.hProcess;
        CloseHandle(child.hThread);
    }
    bool acknowledged = launched && WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0 && alive();
    CloseHandle(ready);
    CloseHandle(parent);
    SetHandleInformation(mapping, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(mutex, HANDLE_FLAG_INHERIT, 0);
    return acknowledged;
}
bool Recovery::alive() const {
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}
std::string Recovery::apply(const Window &w, const Appearance &a, bool focused) {
    if (a.mode == Mode::unchanged) {
        restore(w.identity.hwnd);
        return "Unchanged";
    }
    if (a.mode != Mode::opacity)
        return "Only whole-window opacity is supported";
    if (!alive())
        return "Recovery helper unavailable; no change made";
    if (!same_identity(w.identity) || !exclusion(w.identity.hwnd).empty())
        return "Window no longer eligible";
    if (!lock())
        return "Recovery state busy; no change made";
    Record *record = nullptr;
    for (auto &r : shared->records)
        if (r.used && r.identity.hwnd == w.identity.hwnd) {
            record = &r;
            break;
        }
    if (record &&
        (!same_identity(record->identity) ||
         reinterpret_cast<ULONG_PTR>(GetPropW(w.identity.hwnd, owner_property)) != record->cookie)) {
        *record = {};
        record = nullptr;
    }
    if (record && record->mode != a.mode) {
        restore_record(*record);
        *record = {};
        record = nullptr;
    }
    auto current = capture(w.identity.hwnd, a.mode);
    if (!current) {
        ReleaseMutex(mutex);
        return "Unsupported: original state cannot be read safely";
    }
    if (record && !state_matches(*current, record->after, a.mode)) {
        ReleaseMutex(mutex);
        return "Conflict: app changed its appearance; refresh to retry";
    }
    if (a.mode == Mode::opacity && (focused ? a.active : a.inactive) == 100 &&
        ((record && !record->original.layered) || (!record && !current->layered))) {
        if (record) {
            restore_record(*record);
            *record = {};
        }
        ReleaseMutex(mutex);
        return "Original non-layered appearance retained at 100% opacity";
    }
    if (!record) {
        for (auto &r : shared->records)
            if (!r.used) {
                record = &r;
                break;
            }
        if (!record) {
            ReleaseMutex(mutex);
            return "256-window transaction limit reached";
        }
        if (GetPropW(w.identity.hwnd, owner_property)) {
            ReleaseMutex(mutex);
            return "Another controller owns this window";
        }
        const auto cookie = static_cast<ULONG_PTR>(GetTickCount64() ^ reinterpret_cast<ULONG_PTR>(record) ^
                                                   GetCurrentProcessId()) |
                            1;
        if (!SetPropW(w.identity.hwnd, owner_property, reinterpret_cast<HANDLE>(cookie))) {
            ReleaseMutex(mutex);
            return "Cannot establish window ownership";
        }
        *record = Record{true, w.identity, cookie, a.mode, *current, *current, *current};
    }
    const auto target = desired(record->original, a.mode, focused ? a.active : a.inactive);
    if (state_matches(*current, target, a.mode)) {
        ReleaseMutex(mutex);
        return "Applied (appearance unverified)";
    }
    record->before = *current;
    record->after = target; // Helper can recover either side of an interrupted write.
    const bool ok = set_effect(w.identity.hwnd, a.mode, target);
    const auto after = capture(w.identity.hwnd, a.mode);
    if (!ok || !after || !state_matches(*after, target, a.mode)) {
        // On partial style addition, capture may fail before alpha initialization. Identity and cookie still
        // establish ownership.
        if (same_identity(record->identity) &&
            reinterpret_cast<ULONG_PTR>(GetPropW(w.identity.hwnd, owner_property)) == record->cookie)
            set_effect(w.identity.hwnd, a.mode, record->original);
        RemovePropW(w.identity.hwnd, owner_property);
        *record = {};
        ReleaseMutex(mutex);
        return "Effect failed; rollback attempted";
    }
    record->before = record->after;
    ReleaseMutex(mutex);
    return a.mode == Mode::opacity ? "Whole-window opacity applied (including text)"
                                   : "Material API accepted; visible coverage unverified";
}
bool Recovery::watch_taskbar_companion(const CompanionRecord &record) {
    if (!alive() || !lock())
        return false;
    shared->taskbar_companion = record;
    ReleaseMutex(mutex);
    return true;
}
void Recovery::clear_taskbar_companion() {
    if (!shared || !lock())
        return;
    shared->taskbar_companion = {};
    ReleaseMutex(mutex);
}
void Recovery::restore(HWND hwnd) {
    if (!shared || !lock())
        return;
    for (auto &r : shared->records)
        if (r.used && r.identity.hwnd == hwnd) {
            restore_record(r);
            r = {};
        }
    ReleaseMutex(mutex);
}
void Recovery::forget(HWND hwnd) {
    if (!shared || !lock())
        return;
    for (auto &r : shared->records)
        if (r.used && r.identity.hwnd == hwnd)
            r = {};
    ReleaseMutex(mutex);
}
void Recovery::restore_all() {
    if (!shared || !lock())
        return;
    for (auto &r : shared->records)
        if (r.used) {
            restore_record(r);
            r = {};
        }
    ReleaseMutex(mutex);
}
size_t Recovery::count() const {
    if (!shared)
        return 0;
    size_t count = 0;
    for (const auto &r : shared->records)
        if (r.used)
            ++count;
    return count;
}
bool Recovery::minimize(const Window &w) {
    if (!alive() || !same_identity(w.identity) || !exclusion(w.identity.hwnd).empty() ||
        IsIconic(w.identity.hwnd))
        return false;
    if (!lock())
        return false;
    auto h = w.identity.hwnd;
    for (auto &r : shared->minimized)
        if (r.used && r.identity.hwnd == h) {
            // It was restored since our previous minimize. Renew ownership from
            // its current placement instead of reporting an obsolete record as success.
            restore_minimized_record(r);
            r = {};
            break;
        }
    MinimizedRecord *record = nullptr;
    for (auto &r : shared->minimized)
        if (!r.used) {
            record = &r;
            break;
        }
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    const auto cookie = static_cast<ULONG_PTR>(GetTickCount64() ^ reinterpret_cast<ULONG_PTR>(record)) | 1;
    if (!record || GetPropW(h, focus_property) || !GetWindowPlacement(h, &placement) ||
        !SetPropW(h, focus_property, reinterpret_cast<HANDLE>(cookie))) {
        ReleaseMutex(mutex);
        return false;
    }
    *record = {true, w.identity, cookie, placement};
    ShowWindow(h, SW_MINIMIZE);
    const bool minimized = IsIconic(h) != FALSE;
    if (!minimized) {
        RemovePropW(h, focus_property);
        *record = {};
    }
    ReleaseMutex(mutex);
    return minimized;
}
void Recovery::restore_minimized() {
    if (!shared || !lock())
        return;
    const auto foreground = GetForegroundWindow();
    for (auto &r : shared->minimized)
        if (r.used) {
            restore_minimized_record(r);
            r = {};
        }
    ReleaseMutex(mutex);
    if (foreground && IsWindow(foreground) && !IsIconic(foreground) && GetForegroundWindow() != foreground)
        SetForegroundWindow(foreground);
}
} // namespace wfx
