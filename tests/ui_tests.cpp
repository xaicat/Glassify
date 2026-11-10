#include "app/control_ids.hpp"
#include "core/config.hpp"
#include "platform/windows.hpp"
#include <commctrl.h>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace wfx;
using namespace wfx::ui;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool until(const std::function<bool()> &condition, unsigned milliseconds = 5000) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (condition())
            return true;
        Sleep(10);
    } while (GetTickCount64() < deadline);
    return false;
}
bool alpha_is(HWND target, BYTE expected) {
    BYTE alpha{};
    DWORD flags{};
    COLORREF color{};
    return GetLayeredWindowAttributes(target, &color, &alpha, &flags) && alpha == expected;
}
void click(HWND app, int id) {
    PostMessageW(app, WM_COMMAND, static_cast<WPARAM>(id), 0);
}
} // namespace
int main() {
    using namespace wfx;
    using namespace wfx::ui;
    HWND controller{}, target{}, recreated{};
    PROCESS_INFORMATION child{};
    try {
        const auto scratch = executable_path().parent_path() / L"ui-test-data";
        SetEnvironmentVariableW(L"WINFADEX_DATA_DIR", scratch.c_str());
        SetEnvironmentVariableW(L"WINFADEX_TEST_INSTANCE", L"UITest");
        const auto target_pid = std::to_wstring(GetCurrentProcessId());
        SetEnvironmentVariableW(L"WINFADEX_TEST_TARGET_PID", target_pid.c_str());
        std::filesystem::create_directories(scratch);
        write_atomic(scratch / "config.json", serialize_config(Config{}));
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"WinFadeX.UIProbe";
        RegisterClassW(&cls);
        target = CreateWindowW(cls.lpszClassName, L"Controlled UI test target", WS_OVERLAPPEDWINDOW, 20, 20,
                               320, 180, nullptr, nullptr, cls.hInstance, nullptr);
        require(target != nullptr, "Create UI target");
        ShowWindow(target, SW_SHOWNOACTIVATE);
        const auto program = executable_path().parent_path() / L"Glassify.exe";
        auto command = L"\"" + program.wstring() + L"\" --background   ";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION quit_probe{};
        auto quit_command = L"\"" + program.wstring() + L"\"   --quit   ";
        require(CreateProcessW(program.c_str(), quit_command.data(), nullptr, nullptr, FALSE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &startup, &quit_probe),
                "Launch whitespace quit probe");
        const bool quit_exited =
            until([&] { return WaitForSingleObject(quit_probe.hProcess, 0) == WAIT_OBJECT_0; });
        if (!quit_exited) {
            auto probe_window = FindWindowW(L"WinFadeX.Next.UITest", nullptr);
            DWORD probe_pid{};
            GetWindowThreadProcessId(probe_window, &probe_pid);
            if (probe_pid == quit_probe.dwProcessId)
                click(probe_window, quit_button);
            until([&] { return WaitForSingleObject(quit_probe.hProcess, 0) == WAIT_OBJECT_0; });
        }
        CloseHandle(quit_probe.hThread);
        CloseHandle(quit_probe.hProcess);
        require(quit_exited, "Quit accepts shell whitespace without starting a controller");
        require(CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                               nullptr, nullptr, &startup, &child),
                "Launch actual UI controller");
        require(until([&] {
                    auto found = FindWindowW(L"WinFadeX.Next.UITest", nullptr);
                    DWORD pid{};
                    GetWindowThreadProcessId(found, &pid);
                    if (pid == child.dwProcessId && GetDlgItem(found, quit_button))
                        controller = found;
                    return controller != nullptr;
                }),
                "Controller created its controls");
        // Wait until the initial enumeration and recovery handshake finish.
        DWORD_PTR response{};
        require(SendMessageTimeoutW(controller, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 8000, &response),
                "Controller startup responsive");
        require(!IsWindowVisible(controller), "Background launch accepts trailing shell whitespace");
        require(!GetDlgItem(controller, removed_mode_selector), "Material selector removed");
        require(GetDlgItem(controller, active_slider) && GetDlgItem(controller, inactive_slider),
                "Opacity sliders created");
        require(until([&] { return IsWindowEnabled(GetDlgItem(controller, active_edit)); }),
                "Whole-window mode enables opacity controls");
        const auto path = executable_path().wstring();
        SendMessageW(GetDlgItem(controller, path_edit), WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(path.c_str()));
        SendMessageW(GetDlgItem(controller, active_edit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"85"));
        SendMessageW(GetDlgItem(controller, inactive_edit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"85"));
        click(controller, keep_button);
        require(until([&] { return alpha_is(target, 217); }),
                "Save rule applies 85% opacity through actual UI");
        const auto saved_config = parse_config(read_file(scratch / "config.json"));
        require(saved_config.rules.size() == 1, "UI saved exactly one rule");
        SendMessageW(GetDlgItem(controller, active_edit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"80"));
        SendMessageW(GetDlgItem(controller, inactive_edit), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"80"));
        click(controller, preview_button);
        require(until([&] { return alpha_is(target, 204); }), "Temporary preview applied");
        require(until([&] { return alpha_is(target, 217); }, 18000),
                "15-second preview expires to saved rule");
        for (auto id : {active_slider, inactive_slider}) {
            const auto slider = GetDlgItem(controller, id);
            SendMessageW(slider, TBM_SETPOS, TRUE, 80);
            PostMessageW(controller, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(slider));
        }
        require(until([&] { return alpha_is(target, 204); }),
                "Dragging sliders applies a live temporary preview");
        require(parse_config(read_file(scratch / "config.json")).rules[0].appearance.active == 85,
                "Slider preview does not overwrite saved rules");
        click(controller, cancel_button);
        require(until([&] { return alpha_is(target, 217); }), "Cancel slider preview restores saved opacity");
        recreated = CreateWindowW(cls.lpszClassName, L"New matching target", WS_OVERLAPPEDWINDOW, 40, 40, 320,
                                  180, nullptr, nullptr, cls.hInstance, nullptr);
        require(recreated != nullptr, "Create new matching window");
        ShowWindow(recreated, SW_SHOWNOACTIVATE);
        require(until([&] { return alpha_is(recreated, 217); }),
                "WinEvent applies rule to a new matching window");
        click(controller, pause_button);
        require(until([&] {
                    return !(GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_LAYERED) &&
                           !(GetWindowLongPtrW(recreated, GWL_EXSTYLE) & WS_EX_LAYERED);
                }),
                "Pause restores both windows");
        click(controller, pause_button);
        require(until([&] { return alpha_is(target, 217); }), "Resume reapplies saved rule");
        const bool was_focused = GetForegroundWindow() == target;
        PostMessageW(controller, WM_APP + 5, static_cast<WPARAM>(static_cast<INT_PTR>(-120)),
                     reinterpret_cast<LPARAM>(target));
        require(until([&] { return alpha_is(target, 204); }),
                "Wheel adjusts a window that already has saved app settings");
        const auto item =
            SendMessageW(GetDlgItem(controller, windows_list), LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                         reinterpret_cast<LPARAM>(L"Controlled UI test target"));
        require(item >= 0, "Find original target in window list");
        SendMessageW(GetDlgItem(controller, windows_list), LB_SETCURSEL, item, 0);
        PostMessageW(controller, WM_COMMAND, MAKEWPARAM(windows_list, LBN_SELCHANGE), 0);
        require(until([&] {
                    wchar_t value[16]{};
                    SendMessageW(GetDlgItem(controller, was_focused ? active_edit : inactive_edit),
                                 WM_GETTEXT, 16, reinterpret_cast<LPARAM>(value));
                    return wcscmp(value, L"80") == 0;
                }),
                "Selecting an adjusted window keeps its temporary value in the editor");
        click(controller, keep_button);
        require(until([&] {
                    const auto saved = parse_config(read_file(scratch / "config.json"));
                    return saved.rules.size() == 1 &&
                           (was_focused ? saved.rules[0].appearance.active
                                        : saved.rules[0].appearance.inactive) == 80;
                }),
                "Saving a wheel adjustment updates the existing app rule");
        for (auto id : {active_edit, inactive_edit})
            SendMessageW(GetDlgItem(controller, id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"85"));
        click(controller, keep_button);
        require(until([&] { return alpha_is(target, 217) && alpha_is(recreated, 217); }),
                "Saved app values restored for later tests");
        // Automatic mode is PID-scoped by the test instance, never the user's real windows.
        SendMessageW(GetDlgItem(controller, default_active), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"80"));
        SendMessageW(GetDlgItem(controller, default_inactive), WM_SETTEXT, 0,
                     reinterpret_cast<LPARAM>(L"80"));
        click(controller, save_automation);
        require(until([&] { return parse_config(read_file(scratch / "config.json")).defaults.active == 80; }),
                "Automatic defaults saved");
        SendMessageW(GetDlgItem(controller, auto_quick), BM_SETCHECK, BST_CHECKED, 0);
        click(controller, auto_quick);
        require(until([&] { return parse_config(read_file(scratch / "config.json")).auto_apply; }),
                "Automatic mode enabled");
        require(alpha_is(target, 217), "Saved app opacity takes priority over automatic default");
        SendMessageW(GetDlgItem(controller, rules_list), LB_SETCURSEL, 0, 0);
        click(controller, delete_button);
        require(until([&] { return alpha_is(target, 204) && alpha_is(recreated, 204); }),
                "Automatic defaults cover eligible windows after removing a rule");
        PostMessageW(controller, WM_APP + 5, static_cast<WPARAM>(static_cast<INT_PTR>(-120)),
                     reinterpret_cast<LPARAM>(target));
        require(until([&] { return alpha_is(target, 191); }),
                "Mouse-wheel command changes only the pointer target");
        require(alpha_is(recreated, 204), "Mouse override leaves other windows unchanged");
        for (int i = 0; i < 2; ++i)
            PostMessageW(controller, WM_APP + 5, 60, reinterpret_cast<LPARAM>(target));
        require(until([&] { return alpha_is(target, 204); }),
                "Partial mouse-wheel deltas accumulate correctly");
        click(controller, preset_first);
        require(until([&] { return !(GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_LAYERED); }),
                "Clear preset restores full opacity");
        click(controller, preset_first + 6);
        require(until([&] {
                    auto overlay = GetWindow(target, GW_HWNDPREV);
                    return GetPropW(overlay, L"WFX.TintTarget") == target;
                }),
                "Color preset attaches tint immediately above its target");
        const auto target_item =
            SendMessageW(GetDlgItem(controller, windows_list), LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                         reinterpret_cast<LPARAM>(L"Controlled UI test target"));
        SendMessageW(GetDlgItem(controller, windows_list), LB_SETCURSEL, target_item, 0);
        SendMessageW(controller, WM_COMMAND, MAKEWPARAM(windows_list, LBN_SELCHANGE), 0);
        for (auto id : {active_edit, inactive_edit})
            SendMessageW(GetDlgItem(controller, id), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"60"));
        click(controller, keep_window_button);
        require(until([&] { return alpha_is(target, 153); }),
                "Keep this window retains a window-only appearance");
        require(alpha_is(recreated, 204),
                "Window-only appearance does not affect another window from the same exe");
        ShowWindow(controller, SW_SHOWNOACTIVATE);
        ShowWindow(controller, SW_MINIMIZE);
        require(until([&] { return !IsWindowVisible(controller); }),
                "Minimizing the control panel hides it to the tray");
        click(controller, quit_button);
        require(until([&] { return WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0; }),
                "Quit controller");
        require(!(GetWindowLongPtrW(target, GWL_EXSTYLE) & WS_EX_LAYERED), "Quit restores target");
        DestroyWindow(target);
        DestroyWindow(recreated);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        std::cout << "Actual UI save, preview timeout, new-window event, pause/resume and quit passed\n";
    } catch (const std::exception &e) {
        if (controller)
            click(controller, quit_button);
        if (child.hProcess) {
            until([&] { return WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0; });
            CloseHandle(child.hProcess);
        }
        if (child.hThread)
            CloseHandle(child.hThread);
        if (target)
            DestroyWindow(target);
        if (recreated)
            DestroyWindow(recreated);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
