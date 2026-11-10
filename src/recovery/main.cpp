#include "platform/taskbar.hpp"
#include "recovery/client.hpp"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 5) {
        if (argv)
            LocalFree(argv);
        return 2;
    }
    HANDLE handles[4]{};
    for (int i = 0; i < 4; ++i) {
        wchar_t *end{};
        const auto value = wcstoull(argv[i + 1], &end, 10);
        if (!value || !end || *end) {
            LocalFree(argv);
            return 2;
        }
        handles[i] = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(value));
    }
    LocalFree(argv);
    auto state = static_cast<wfx::SharedState *>(
        MapViewOfFile(handles[0], FILE_MAP_ALL_ACCESS, 0, 0, sizeof(wfx::SharedState)));
    if (!state || state->magic != 0x57465833)
        return 3;
    SetEvent(handles[2]);
    if (WaitForSingleObject(handles[3], INFINITE) == WAIT_OBJECT_0) {
        const auto locked = WaitForSingleObject(handles[1], 5000);
        if (locked == WAIT_OBJECT_0 || locked == WAIT_ABANDONED) {
            const auto companion = state->taskbar_companion;
            const auto foreground = GetForegroundWindow();
            for (auto &r : state->records)
                if (r.used) {
                    wfx::restore_record(r);
                    r = {};
                }
            for (auto &r : state->minimized)
                if (r.used) {
                    wfx::restore_minimized_record(r);
                    r = {};
                }
            ReleaseMutex(handles[1]);
            if (companion.pid) {
                // The parent can exit while the owned companion is still initializing.
                for (int retry = 0; retry < 80; ++retry) {
                    if (wfx::close_taskbar_companion(companion))
                        break;
                    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, companion.pid);
                    const bool exited = !p || WaitForSingleObject(p, 0) != WAIT_TIMEOUT;
                    if (p)
                        CloseHandle(p);
                    if (exited)
                        break;
                    Sleep(100);
                }
            }

            if (foreground && IsWindow(foreground) && !IsIconic(foreground) &&
                GetForegroundWindow() != foreground)
                SetForegroundWindow(foreground);
        }
    }
    UnmapViewOfFile(state);
    for (auto handle : handles)
        CloseHandle(handle);
    return 0;
}
