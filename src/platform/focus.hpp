#pragma once
#include "recovery/client.hpp"
#include <map>

namespace wfx {
class FocusSession {
    Recovery &recovery;
    std::optional<Identity> owner;
    std::map<HWND, Identity> minimized;
    std::map<HWND, Identity> respected;

  public:
    explicit FocusSession(Recovery &r) : recovery(r) {}
    void update(HWND foreground, const std::vector<Window> &windows, bool enabled,
                bool fullscreen_only = false, bool same_monitor = true, bool restore_on_switch = true);
    void stop() {
        recovery.restore_minimized();
        owner.reset();
        minimized.clear();
        respected.clear();
    }
    HWND target() const {
        return owner ? owner->hwnd : nullptr;
    }
    size_t minimized_count() const {
        return minimized.size();
    }
};
} // namespace wfx
