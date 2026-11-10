#include "platform/mouse.hpp"

namespace wfx {
MouseControl *MouseControl::current{};
LRESULT CALLBACK MouseControl::procedure(int code, WPARAM w, LPARAM l) {
    auto self = current;
    if (code == HC_ACTION && self && self->enabled && w == WM_MOUSEWHEEL &&
        (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        const auto info = reinterpret_cast<MSLLHOOKSTRUCT *>(l);
        auto window = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
        if (auto target = static_cast<HWND>(GetPropW(window, L"WFX.TintTarget")))
            window = target;
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (window && pid != GetCurrentProcessId()) {
            const int delta = static_cast<short>(HIWORD(info->mouseData));
            if (PostMessageW(self->target, wheel_message, static_cast<WPARAM>(static_cast<INT_PTR>(delta)),
                             reinterpret_cast<LPARAM>(window)))
                return 1;
        }
    }
    return CallNextHookEx(nullptr, code, w, l);
}
bool MouseControl::start(HWND owner) {
    if (thread.joinable())
        return hook != nullptr;
    target = owner;
    stopping = false;
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready)
        return false;
    thread = std::thread([this] {
        current = this;
        MSG m{};
        PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        thread_id = GetCurrentThreadId();
        hook = SetWindowsHookExW(WH_MOUSE_LL, procedure, GetModuleHandleW(nullptr), 0);
        SetEvent(ready);
        if (hook) {
            while (!stopping && GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            UnhookWindowsHookEx(hook);
        }
        hook = nullptr;
        current = nullptr;
    });
    WaitForSingleObject(ready, 5000);
    return hook != nullptr;
}
void MouseControl::stop() {
    enabled = false;
    stopping = true;
    if (thread.joinable()) {
        PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
        thread.join();
    }
    if (ready) {
        CloseHandle(ready);
        ready = nullptr;
    }
    thread_id = 0;
}
} // namespace wfx
