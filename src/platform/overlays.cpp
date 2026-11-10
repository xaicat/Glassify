#include "platform/overlays.hpp"
#include <dwmapi.h>

namespace wfx {
COLORREF tint_color(int index) {
    const COLORREF colors[]{RGB(0, 0, 0),      RGB(35, 94, 218), RGB(123, 65, 212), RGB(198, 54, 112),
                            RGB(196, 117, 25), RGB(24, 139, 99), RGB(72, 88, 112),  RGB(6, 12, 25)};
    return colors[index >= 0 && index < 8 ? index : 0];
}
const wchar_t *tint_name(int index) {
    static const wchar_t *names[]{L"None",  L"Blue",  L"Violet", L"Rose",
                                  L"Amber", L"Green", L"Slate",  L"Night"};
    return names[index >= 0 && index < 8 ? index : 0];
}
static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (m == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (m == WM_ERASEBKGND) {
        RECT r{};
        GetClientRect(h, &r);
        auto b = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA)));
        FillRect(reinterpret_cast<HDC>(w), &r, b);
        DeleteObject(b);
        return 1;
    }
    if (m == WM_PAINT) {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(h, &ps);
        auto b = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(h, GWLP_USERDATA)));
        FillRect(dc, &ps.rcPaint, b);
        DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void Overlays::update(const Window &w, const Appearance &a) {
    const auto h = w.identity.hwnd;
    if (a.tint == 0 || a.tint_strength == 0 || !same_identity(w.identity) || IsIconic(h) ||
        !IsWindowVisible(h) || GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) {
        remove(h);
        return;
    }
    auto found = entries.find(h);
    if (found != entries.end() && !same_identity(found->second.identity)) {
        remove(h);
        found = entries.end();
    }
    if (found == entries.end()) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"WinFadeX.Tint";
        RegisterClassW(&cls);
        auto overlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
            L"Window color tint", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
        if (!overlay)
            return;
        SetPropW(overlay, L"WFX.TintTarget", h);
        found = entries.emplace(h, Entry{w.identity, overlay, -1, -1}).first;
    }
    auto &e = found->second;
    if (e.color != a.tint) {
        e.color = a.tint;
        SetWindowLongPtrW(e.overlay, GWLP_USERDATA, tint_color(a.tint));
        InvalidateRect(e.overlay, nullptr, TRUE);
    }
    if (e.strength != a.tint_strength) {
        e.strength = a.tint_strength;
        SetLayeredWindowAttributes(e.overlay, 0, static_cast<BYTE>((a.tint_strength * 255 + 50) / 100),
                                   LWA_ALPHA);
    }
    reposition(h);
}
void Overlays::reposition(HWND h) {
    auto found = entries.find(h);
    if (found == entries.end())
        return;
    auto &e = found->second;
    if (!same_identity(e.identity) || !IsWindowVisible(h) || IsIconic(h)) {
        remove(h);
        return;
    }
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(h, &r);
    HWND previous = GetWindow(h, GW_HWNDPREV);
    const bool zoomed = IsZoomed(h) != FALSE;
    const bool geometry_changed = !e.positioned || !EqualRect(&r, &e.geometry) || zoomed != e.zoomed;
    if (!geometry_changed && previous == e.overlay && IsWindowVisible(e.overlay))
        return;
    UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    if (previous == e.overlay)
        flags |= SWP_NOZORDER;
    else if (previous && (GetWindowLongPtrW(previous, GWL_EXSTYLE) & WS_EX_TOPMOST))
        previous = HWND_TOP;
    SetWindowPos(e.overlay, previous ? previous : HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 flags);
    if (!geometry_changed)
        return;
    e.geometry = r;
    e.positioned = true;
    e.zoomed = zoomed;
    RECT own{};
    GetWindowRect(e.overlay, &own);
    if (!IsZoomed(h)) {
        auto region = CreateRoundRectRgn(0, 0, own.right - own.left + 1, own.bottom - own.top + 1, 16, 16);
        if (!SetWindowRgn(e.overlay, region, TRUE))
            DeleteObject(region);
    } else
        SetWindowRgn(e.overlay, nullptr, TRUE);
}
void Overlays::remove(HWND h) {
    auto found = entries.find(h);
    if (found != entries.end()) {
        DestroyWindow(found->second.overlay);
        entries.erase(found);
    }
}
void Overlays::clear() {
    while (!entries.empty())
        remove(entries.begin()->first);
}
HWND Overlays::handle(HWND h) const {
    const auto found = entries.find(h);
    return found == entries.end() ? nullptr : found->second.overlay;
}
} // namespace wfx
