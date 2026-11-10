#pragma once
#include <atomic>
#include <thread>
#include <windows.h>

namespace wfx {
constexpr UINT wheel_message = WM_APP + 5;
class MouseControl {
    std::thread thread;
    std::atomic<DWORD> thread_id{};
    std::atomic<bool> enabled{};
    std::atomic<bool> stopping{};
    HWND target{};
    std::atomic<HHOOK> hook{};
    HANDLE ready{};
    static MouseControl *current;
    static LRESULT CALLBACK procedure(int code, WPARAM w, LPARAM l);

  public:
    ~MouseControl() {
        stop();
    }
    bool start(HWND owner);
    void set_enabled(bool value) {
        enabled = value;
    }
    void stop();
};
} // namespace wfx
