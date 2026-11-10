#pragma once
#include "recovery/client.hpp"

namespace wfx {
// A dedicated opt-in route. Normal app rules always exclude shell windows.
std::optional<Window> inspect_taskbar(HWND hwnd);
bool close_taskbar_companion(const CompanionRecord &record);
class Taskbar {
    Recovery &recovery;
    HANDLE process{};
    CompanionRecord companion;
    bool stopping = false, failed = false;
    int applied_opacity = -1;
    std::string status;

  public:
    explicit Taskbar(Recovery &r) : recovery(r) {}
    ~Taskbar() {
        restore();
        if (process)
            CloseHandle(process);
    }
    std::string apply(int opacity);
    void restore();
    static bool installed();
    void poll();
    bool running() const {
        return process && !stopping && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
};
} // namespace wfx
