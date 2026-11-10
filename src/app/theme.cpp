#include "app/theme.hpp"
#include <algorithm>
#include <uxtheme.h>

namespace wfx::ui {
static LRESULT CALLBACK hover(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_MOUSEMOVE && !GetPropW(h, L"WFX.Hover")) {
        SetPropW(h, L"WFX.Hover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_MOUSELEAVE || m == WM_NCDESTROY) {
        RemovePropW(h, L"WFX.Hover");
        if (m == WM_NCDESTROY)
            RemoveWindowSubclass(h, hover, 2);
        else
            InvalidateRect(h, nullptr, FALSE);
    }
    return DefSubclassProc(h, m, w, l);
}
void rounded(HDC dc, RECT r, COLORREF fill, COLORREF stroke, int radius) {
    auto brush = CreateSolidBrush(fill);
    auto pen = CreatePen(PS_SOLID, 1, stroke);
    auto ob = SelectObject(dc, brush), op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(brush);
    DeleteObject(pen);
}
void draw_text(HDC dc, HFONT font, const std::wstring &s, RECT r, COLORREF color, UINT flags) {
    auto old = SelectObject(dc, font);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags);
    SelectObject(dc, old);
}
static LRESULT CALLBACK checkbox(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_PAINT || m == WM_PRINTCLIENT) {
        PAINTSTRUCT ps{};
        HDC dc = m == WM_PAINT ? BeginPaint(h, &ps) : reinterpret_cast<HDC>(w);
        RECT r{};
        GetClientRect(h, &r);
        const auto bg = static_cast<COLORREF>(reinterpret_cast<ULONG_PTR>(GetPropW(h, L"WFX.BG")) - 1);
        auto brush = CreateSolidBrush(bg);
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        const bool checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const int side = MulDiv(18, GetDpiForWindow(h), 96), top = (r.bottom - side) / 2;
        RECT box{0, top, side, top + side};
        rounded(dc, box, checked ? accent : canvas, checked ? accent : border, 6);
        if (checked) {
            auto p = CreatePen(PS_SOLID, 2, canvas);
            auto old = SelectObject(dc, p);
            MoveToEx(dc, side / 4, top + side / 2, nullptr);
            LineTo(dc, side * 4 / 9, top + side * 3 / 4);
            LineTo(dc, side * 4 / 5, top + side / 4);
            SelectObject(dc, old);
            DeleteObject(p);
        }
        wchar_t value[512]{};
        GetWindowTextW(h, value, 512);
        r.left = side + 10;
        draw_text(dc, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)), value, r,
                  IsWindowEnabled(h) ? text_color : muted);
        if (GetFocus() == h) {
            r.left = 0;
            RECT focus{0, top, side, top + side};
            auto pen = CreatePen(PS_SOLID, 2, accent);
            auto old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, focus.left, focus.top, focus.right, focus.bottom, 6, 6);
            SelectObject(dc, old_pen);
            SelectObject(dc, old_brush);
            DeleteObject(pen);
        }
        if (m == WM_PAINT)
            EndPaint(h, &ps);
        return 0;
    }
    const auto result = DefSubclassProc(h, m, w, l);
    if (m == BM_SETCHECK || m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_ENABLE)
        InvalidateRect(h, nullptr, TRUE);
    if (m == WM_NCDESTROY) {
        RemovePropW(h, L"WFX.BG");
        RemoveWindowSubclass(h, checkbox, 1);
    }
    return result;
}
void Theme::initialize(unsigned dpi) {
    for (auto f : {body, small, title, brand})
        if (f)
            DeleteObject(f);
    auto make = [&](int size, int weight) {
        return CreateFontW(-MulDiv(size, dpi, 96), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           L"Segoe UI");
    };
    body = make(14, FW_NORMAL);
    small = make(12, FW_NORMAL);
    title = make(24, FW_SEMIBOLD);
    brand = make(20, FW_SEMIBOLD);
    if (!canvas_brush)
        canvas_brush = CreateSolidBrush(canvas);
    if (!card_brush)
        card_brush = CreateSolidBrush(card);
    if (!sidebar_brush)
        sidebar_brush = CreateSolidBrush(sidebar);
}
void Theme::set_light(bool light, const std::string &variant) {
    ui_light = light;
    canvas = light ? RGB(247, 248, 250) : RGB(12, 20, 36);
    sidebar = light ? RGB(255, 255, 255) : RGB(10, 17, 32);
    card = light ? RGB(255, 255, 255) : RGB(17, 30, 51);
    text_color = light ? RGB(26, 27, 35) : RGB(229, 237, 250);
    muted = light ? RGB(108, 111, 123) : RGB(168, 186, 211);
    accent = light ? RGB(22, 125, 146) : RGB(140, 188, 255);
    border = light ? RGB(225, 227, 232) : RGB(48, 69, 96);
    if (variant == "vscode") {
        canvas = RGB(31, 31, 31);
        sidebar = RGB(24, 24, 24);
        card = RGB(37, 37, 38);
        text_color = RGB(230, 230, 230);
        muted = RGB(175, 175, 184);
        accent = RGB(117, 190, 255);
        border = RGB(60, 60, 60);
    } else if (variant == "ember") {
        canvas = RGB(24, 24, 26);
        sidebar = RGB(20, 20, 22);
        card = RGB(33, 33, 36);
        text_color = RGB(244, 240, 236);
        muted = RGB(190, 180, 173);
        accent = RGB(255, 128, 85);
        border = RGB(62, 57, 55);
    } else if (light)
        accent = RGB(60, 60, 67);
    for (auto b : {canvas_brush, card_brush, sidebar_brush})
        if (b)
            DeleteObject(b);
    canvas_brush = CreateSolidBrush(canvas);
    card_brush = CreateSolidBrush(card);
    sidebar_brush = CreateSolidBrush(sidebar);
}
Theme::~Theme() {
    for (auto f : {body, small, title, brand})
        if (f)
            DeleteObject(f);
    for (auto b : {canvas_brush, card_brush, sidebar_brush})
        if (b)
            DeleteObject(b);
}
void Theme::apply(HWND h, COLORREF bg) {
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
    SetPropW(h, L"WFX.BG", reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(bg) + 1));
    wchar_t cls[64]{};
    GetClassNameW(h, cls, 64);
    if (wcscmp(cls, L"Button") == 0 && (GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW)
        SetWindowSubclass(h, hover, 2, 0);
    if (wcscmp(cls, L"Button") == 0 && (GetWindowLongPtrW(h, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        SetWindowTheme(h, L"", L"");
        SetWindowSubclass(h, checkbox, 1, 0);
    }
}
HBRUSH Theme::color(HDC dc, HWND h) {
    auto prop = reinterpret_cast<ULONG_PTR>(GetPropW(h, L"WFX.BG"));
    auto bg = prop ? static_cast<COLORREF>(prop - 1) : card;
    SetTextColor(dc, IsWindowEnabled(h) && !GetPropW(h, L"WFX.Muted") ? text_color : muted);
    SetBkColor(dc, bg);
    SetBkMode(dc, TRANSPARENT);
    return bg == canvas ? canvas_brush : bg == sidebar ? sidebar_brush : card_brush;
}
void Theme::draw_button(const DRAWITEMSTRUCT &d, bool selected, bool primary) {
    RECT r = d.rcItem;
    const bool down = (d.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(d.hwndItem, L"WFX.Hover") != nullptr;
    const auto bg = primary       ? (hot ? RGB(148, 230, 240) : accent)
                    : selected    ? (ui_light ? RGB(231, 244, 246) : RGB(26, 43, 46))
                    : down || hot ? (ui_light ? RGB(234, 236, 239) : RGB(48, 69, 96))
                                  : (ui_light ? RGB(244, 245, 247) : RGB(24, 41, 64));
    rounded(d.hDC, r, bg, selected || (d.itemState & ODS_FOCUS) ? accent : primary ? bg : border, 8);
    wchar_t label[512]{};
    GetWindowTextW(d.hwndItem, label, 512);
    std::wstring s = label;
    const auto split = s.find(L'\n');
    r.left += 14;
    r.right -= 12;
    if (split == std::wstring::npos)
        draw_text(d.hDC, body, s, r, !IsWindowEnabled(d.hwndItem) ? muted : primary ? canvas : text_color);
    else {
        RECT line = r;
        line.top += 15;
        line.bottom = line.top + 24;
        draw_text(d.hDC, body, s.substr(0, split), line, text_color);
        line.top += 28;
        line.bottom = r.bottom - 6;
        draw_text(d.hDC, small, s.substr(split + 1), line, muted, DT_LEFT | DT_WORDBREAK);
    }
}
LRESULT Theme::trackbar(NMCUSTOMDRAW *d) {
    if (d->dwDrawStage == CDDS_PREPAINT) {
        RECT r{};
        GetClientRect(d->hdr.hwndFrom, &r);
        FillRect(d->hdc, &r, card_brush);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (d->dwDrawStage == CDDS_ITEMPREPAINT) {
        RECT r = d->rc;
        if (d->dwItemSpec == TBCD_CHANNEL) {
            r.top = (r.top + r.bottom) / 2 - 2;
            r.bottom = r.top + 5;
            rounded(d->hdc, r, border, border, 4);
        } else if (d->dwItemSpec == TBCD_THUMB) {
            r.left += 1;
            r.right -= 1;
            rounded(d->hdc, r, accent, accent, 10);
        }
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}
} // namespace wfx::ui
