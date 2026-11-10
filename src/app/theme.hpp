#pragma once
#include <windows.h>

#include <commctrl.h>
#include <string>

namespace wfx::ui {
inline bool ui_light = false;
inline COLORREF canvas = RGB(12, 20, 36), sidebar = RGB(10, 17, 32), card = RGB(17, 30, 51);
inline COLORREF text_color = RGB(229, 237, 250), muted = RGB(168, 186, 211), accent = RGB(140, 188, 255);
inline COLORREF border = RGB(48, 69, 96);
class Theme {
  public:
    HFONT body{}, small{}, title{}, brand{};
    HBRUSH canvas_brush{}, card_brush{}, sidebar_brush{};
    void initialize(unsigned dpi);
    void set_light(bool light, const std::string &variant = "");
    ~Theme();
    void apply(HWND control, COLORREF background = card);
    HBRUSH color(HDC dc, HWND control);
    void draw_button(const DRAWITEMSTRUCT &item, bool selected = false, bool primary = false);
    LRESULT trackbar(NMCUSTOMDRAW *draw);
};
void rounded(HDC dc, RECT rect, COLORREF fill, COLORREF stroke = border, int radius = 8);
void draw_text(HDC dc, HFONT font, const std::wstring &text, RECT rect, COLORREF color,
               UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE);
} // namespace wfx::ui
