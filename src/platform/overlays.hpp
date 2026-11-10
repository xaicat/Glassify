#pragma once
#include "platform/windows.hpp"
#include <map>

namespace wfx {
COLORREF tint_color(int index);
const wchar_t *tint_name(int index);
class Overlays {
    struct Entry {
        Identity identity;
        HWND overlay{};
        int color{}, strength{};
        RECT geometry{};
        bool positioned{}, zoomed{};
    };
    std::map<HWND, Entry> entries;

  public:
    ~Overlays() {
        clear();
    }
    void update(const Window &target, const Appearance &appearance);
    void reposition(HWND target);
    void remove(HWND target);
    void clear();
    HWND handle(HWND target) const;
    size_t count() const {
        return entries.size();
    }
};
} // namespace wfx
