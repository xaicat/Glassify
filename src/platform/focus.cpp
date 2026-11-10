#include "platform/focus.hpp"

namespace wfx {
void FocusSession::update(HWND foreground, const std::vector<Window> &windows, bool enabled,
                          bool fullscreen_only, bool same_monitor, bool restore_on_switch) {
    if (!enabled) {
        stop();
        return;
    }
    wchar_t foreground_class[128]{};
    GetClassNameW(foreground, foreground_class, 128);
    if (owner && IsIconic(owner->hwnd) &&
        (wcscmp(foreground_class, L"Progman") == 0 || wcscmp(foreground_class, L"WorkerW") == 0))
        return; // Show Desktop must not be undone by restoring our minimized windows.
    if (owner && (!same_identity(*owner) || IsIconic(owner->hwnd) || !focus_window(owner->hwnd)))
        stop();
    std::erase_if(minimized, [](const auto &item) { return !same_identity(item.second); });
    std::erase_if(respected, [](const auto &item) { return !same_identity(item.second); });
    // A menu, owner dialog, taskbar, or this app's panel must not end a valid session.
    auto foreground_window = inspect(foreground, false, true);
    if (!foreground_window && owner)
        return;
    if (owner && foreground != owner->hwnd && GetAncestor(foreground, GA_ROOTOWNER) == owner->hwnd)
        return;
    // Our own settings/tint windows never start a focus session.
    auto candidate = focus_window(foreground) && !IsIconic(foreground) &&
                             (!fullscreen_only || fullscreen_window(foreground))
                         ? foreground_window
                         : std::nullopt;
    if (!candidate) {
        if (owner && restore_on_switch)
            stop();
        return;
    }
    if (owner && owner->hwnd != foreground &&
        (same_monitor && MonitorFromWindow(owner->hwnd, MONITOR_DEFAULTTONEAREST) !=
                             MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST)))
        stop();
    // Transfer a same-display session without restoring/minimizing every window again.
    owner = candidate->identity;
    const auto monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    for (const auto &w : windows) {
        auto h = w.identity.hwnd;
        if (h == foreground) {
            minimized.erase(h);
            respected.erase(h);
            continue;
        }
        if (auto it = minimized.find(h); it != minimized.end() && same_identity(it->second) && !IsIconic(h)) {
            respected[h] = it->second;
            minimized.erase(it);
        }
        if (auto it = respected.find(h); it != respected.end() && same_identity(it->second))
            continue;
        if (h == foreground || GetWindow(h, GW_OWNER) || IsIconic(h) ||
            (same_monitor && MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != monitor))
            continue;
        if (recovery.minimize(w))
            minimized[h] = w.identity;
    }
}
} // namespace wfx
