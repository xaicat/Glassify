#pragma once
#include "platform/windows.hpp"
#include <array>

namespace wfx {
constexpr size_t record_limit = 256;
struct SharedState {
    unsigned magic = 0x57465833;
    CompanionRecord taskbar_companion{};
    Record records[record_limit]{};
    MinimizedRecord minimized[record_limit]{};
};
class Recovery {
    HANDLE mapping{}, mutex{}, process{};
    SharedState *shared{};
    bool lock();

  public:
    Recovery() = default;
    ~Recovery();
    Recovery(const Recovery &) = delete;
    Recovery &operator=(const Recovery &) = delete;
    bool start();
    bool alive() const;
    DWORD process_id() const {
        return process ? GetProcessId(process) : 0;
    }
    std::string apply(const Window &window, const Appearance &appearance, bool focused);
    bool watch_taskbar_companion(const CompanionRecord &record);
    void clear_taskbar_companion();
    void restore(HWND hwnd);
    void forget(HWND hwnd);
    void restore_all();
    size_t count() const;
    bool minimize(const Window &window);
    void restore_minimized();
};
} // namespace wfx
