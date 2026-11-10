#include "integrations/terminal.hpp"
#include "platform/focus.hpp"
#include "platform/mouse.hpp"
#include "platform/overlays.hpp"
#include "platform/taskbar.hpp"
#include "platform/windows.hpp"
#include "recovery/client.hpp"
#include <dwmapi.h>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void run_probe(std::wstring arguments) {
    auto command = L"\"" + wfx::executable_path().wstring() + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                         &startup, &child),
          "Launch focus probe");
    auto deadline = GetTickCount64() + 10000;
    while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        MsgWaitForMultipleObjects(1, &child.hProcess, FALSE, 10, QS_ALLINPUT);
    }
    DWORD result = 1;
    GetExitCodeProcess(child.hProcess, &result);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    if (result != 0) {
        const auto diagnostic = wfx::data_directory() / "controller-probe-error.txt";
        throw std::runtime_error("Focus probe failed: " + (std::filesystem::exists(diagnostic)
                                                               ? wfx::read_file(diagnostic)
                                                               : std::to_string(result)));
    }
}
int main(int argc, char **argv) {
    using namespace wfx;
    try {
        if (!GetEnvironmentVariableW(L"WINFADEX_DATA_DIR", nullptr, 0)) {
            const auto scratch = executable_path().parent_path() / L"test-data";
            SetEnvironmentVariableW(L"WINFADEX_DATA_DIR", scratch.c_str());
        }
        if (argc == 3 && std::string(argv[1]) == "--crash-controller") {
            const auto target = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(std::stoull(argv[2])));
            Recovery recovery;
            check(recovery.start(), "Recovery handshake");
            const auto window = inspect(target);
            check(window.has_value(), "Foreign target eligible");
            Appearance appearance;
            appearance.mode = Mode::opacity;
            appearance.active = 85;
            appearance.inactive = 65;
            const auto result = recovery.apply(*window, appearance, true);
            check(result.find("applied") != std::string::npos, result.c_str());
            check(recovery.apply(*window, appearance, false).find("applied") != std::string::npos,
                  "Inactive state applied");
            BYTE inactive{};
            DWORD flags{};
            COLORREF key{};
            check(GetLayeredWindowAttributes(target, &key, &inactive, &flags) && inactive == 166,
                  "Distinct inactive opacity readback");
            check(recovery.minimize(*window), "Focus minimization registered before controller crash");
            // No C++ destructors run: the helper must do the restoration.
            TerminateProcess(GetCurrentProcess(), 0);
        }
        if (argc == 4 &&
            (std::string(argv[1]) == "--focus-controller" || std::string(argv[1]) == "--focus-baseline")) {
            auto owner = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(std::stoull(argv[2])));
            auto other = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(std::stoull(argv[3])));
            Recovery recovery;
            check(recovery.start(), "Focus recovery handshake");
            FocusSession focus(recovery);
            auto first = inspect(owner);
            auto second = inspect(other);
            check(first && second, "Focus identities available");
            focus.update(owner, {*first, *second}, true);
            check(IsIconic(other), "Focus mode minimizes the other window");
            const bool baseline = std::string(argv[1]) == "--focus-baseline";
            if (!baseline) {
                focus.update(nullptr, {*first, *second}, true);
                check(IsIconic(other) && focus.target() == owner,
                      "Transient foreground keeps focus session stable");
                ShowWindow(other, SW_SHOWNOACTIVATE);
                focus.update(owner, {*first, *second}, true);
                check(!IsIconic(other), "Manual restore is respected for this session");
                focus.stop();
                focus.update(owner, {*first, *second}, true, true);
                check(!IsIconic(other), "Fullscreen-only mode ignores ordinary maximized windows");
                const auto owner_style = GetWindowLongPtrW(owner, GWL_STYLE);
                SetWindowLongPtrW(owner, GWL_STYLE, owner_style & ~WS_CAPTION);
                focus.update(owner, {*first, *second}, true, true);
                check(!IsIconic(other), "Fullscreen-only mode rejects a borderless maximized window that "
                                        "does not cover its monitor");
                SetWindowLongPtrW(owner, GWL_STYLE, owner_style);
                focus.stop();
                focus.update(owner, {*first, *second}, true);
                check(IsIconic(other), "New focus session can minimize a restored companion");
                const auto other_style = GetWindowLongPtrW(other, GWL_STYLE);
                ShowWindow(other, SW_SHOWNOACTIVATE);
                SetWindowLongPtrW(other, GWL_STYLE, (other_style & ~WS_MINIMIZE) | WS_MAXIMIZE);
                focus.update(other, {*first, *second}, true);
                check(IsIconic(owner), "Focus transfers to the restored maximized companion");
                ShowWindow(owner, SW_SHOWNOACTIVATE);
                SetWindowLongPtrW(owner, GWL_STYLE, owner_style);
                focus.update(owner, {*first, *second}, true);
                check(IsIconic(other), "Repeated owner transfer renews the previous minimization record");
                focus.stop();
                SetWindowLongPtrW(other, GWL_STYLE, other_style & ~WS_MINIMIZE);
            }
            focus.update(nullptr, {}, false);
            check((IsIconic(other) != FALSE) == baseline,
                  "Focus restore preserves already-minimized baseline");
            return 0;
        }
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"WinFadeX.Test";
        RegisterClassW(&cls);
        auto hwnd = CreateWindowW(cls.lpszClassName, L"Owned test", WS_OVERLAPPEDWINDOW, 40, 40, 400, 200,
                                  nullptr, nullptr, cls.hInstance, nullptr);
        check(hwnd != nullptr, "Create test window");
        check(!inspect_taskbar(hwnd), "Ordinary app cannot enter the taskbar backend");
        {
            MouseControl mouse;
            check(mouse.start(hwnd), "Dedicated mouse hook starts");
            mouse.set_enabled(false);
            mouse.stop();
            check(mouse.start(hwnd), "Mouse hook can restart cleanly");
            mouse.stop();
        }
        auto original = capture(hwnd, Mode::opacity);
        check(original.has_value(), "Read alpha state");
        const auto next = desired(*original, Mode::opacity, 85);
        auto id = identify(hwnd);
        check(id.has_value(), "Read window identity");
        SetPropW(hwnd, owner_property, reinterpret_cast<HANDLE>(123));
        Record record{true, *id, 123, Mode::opacity, *original, *original, next};
        check(set_effect(hwnd, Mode::opacity, next), "Apply alpha");
        check(state_matches(*capture(hwnd, Mode::opacity), next, Mode::opacity), "Read alpha back");
        auto styles = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, styles | WS_EX_APPWINDOW);
        check(restore_record(record), "Restore owned alpha");
        check((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0, "Unrelated style retained");
        check((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0, "Layered bit removed");
        SetPropW(hwnd, owner_property, reinterpret_cast<HANDLE>(123));
        set_effect(hwnd, Mode::opacity, next);
        SetLayeredWindowAttributes(hwnd, 0, 77, LWA_ALPHA);
        check(!restore_record(record), "External alpha edit preserved");
        BYTE alpha{};
        DWORD flags{};
        COLORREF color{};
        GetLayeredWindowAttributes(hwnd, &color, &alpha, &flags);
        check(alpha == 77, "External alpha unchanged");
        set_effect(hwnd, Mode::opacity, *original);
        check(!capture(hwnd, Mode::mica), "Foreign DWM material backend removed");
        DestroyWindow(hwnd);
        check(!same_identity(*id), "Destroyed HWND rejected");
        // A foreign controller changes this test-owned window and is forcibly terminated.
        hwnd = CreateWindowW(cls.lpszClassName, L"Recovery test", WS_OVERLAPPEDWINDOW, 40, 40, 400, 200,
                             nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        check(exclusion(hwnd, true).empty(), "Recovery target eligibility");
        auto command = L"\"" + executable_path().wstring() + L"\" --crash-controller " +
                       std::to_wstring(reinterpret_cast<ULONG_PTR>(hwnd));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             nullptr, &startup, &child),
              "Launch controller probe");
        const auto controller_deadline = GetTickCount64() + 10000;
        while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT &&
               GetTickCount64() < controller_deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            MsgWaitForMultipleObjects(1, &child.hProcess, FALSE, 20, QS_ALLINPUT);
        }
        check(WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0, "Controller probe completes");
        DWORD exit_code{};
        GetExitCodeProcess(child.hProcess, &exit_code);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        check(exit_code == 0, "Controller probe succeeds");
        const auto deadline = GetTickCount64() + 3000;
        while ((GetPropW(hwnd, owner_property) || GetPropW(hwnd, focus_property)) &&
               GetTickCount64() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        check(!GetPropW(hwnd, owner_property), "Recovery helper clears ownership after crash");
        check(!(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED),
              "Recovery helper restores original style");
        check(!IsIconic(hwnd) && !GetPropW(hwnd, focus_property),
              "Helper restores focus-minimized window after controller crash");
        auto other = CreateWindowW(cls.lpszClassName, L"Focus companion", WS_OVERLAPPEDWINDOW, 60, 60, 400,
                                   200, nullptr, nullptr, cls.hInstance, nullptr);
        ShowWindow(other, SW_SHOWNOACTIVATE);
        auto owner_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, owner_style | WS_MAXIMIZE);
        const auto arguments = std::to_wstring(reinterpret_cast<ULONG_PTR>(hwnd)) + L" " +
                               std::to_wstring(reinterpret_cast<ULONG_PTR>(other));
        run_probe(L"--focus-controller " + arguments);
        ShowWindow(other, SW_MINIMIZE);
        run_probe(L"--focus-baseline " + arguments);
        SetWindowLongPtrW(hwnd, GWL_STYLE, owner_style);
        DestroyWindow(other);
        Overlays overlays;
        auto window = inspect(hwnd, true);
        check(window.has_value(), "Overlay target identity available");
        Appearance tinted{Mode::opacity, 90, 80, 1, 15};
        overlays.update(*window, tinted);
        auto overlay = overlays.handle(hwnd);
        check(overlay != nullptr, "Tint overlay created");
        const auto overlay_style = GetWindowLongPtrW(overlay, GWL_EXSTYLE);
        check((overlay_style & WS_EX_TRANSPARENT) && (overlay_style & WS_EX_NOACTIVATE) &&
                  !(overlay_style & WS_EX_TOPMOST),
              "Tint is click-through, non-activating and not globally topmost");
        SetWindowPos(hwnd, nullptr, 100, 120, 440, 260, SWP_NOZORDER | SWP_NOACTIVATE);
        overlays.reposition(hwnd);
        RECT bounds{}, overlay_bounds{};
        if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))))
            GetWindowRect(hwnd, &bounds);
        GetWindowRect(overlay, &overlay_bounds);
        check(EqualRect(&bounds, &overlay_bounds), "Tint follows target movement and size");
        ShowWindow(hwnd, SW_MINIMIZE);
        overlays.reposition(hwnd);
        check(overlays.count() == 0, "Tint removed while target minimized");
        DestroyWindow(hwnd);
        // Repeated owned-window lifecycles must not reuse a stale identity/property.
        for (int i = 0; i < 10000; ++i) {
            auto target = CreateWindowW(cls.lpszClassName, L"Churn", WS_OVERLAPPED, 0, 0, 1, 1, nullptr,
                                        nullptr, cls.hInstance, nullptr);
            check(target != nullptr, "Create churn window");
            check(!GetPropW(target, owner_property), "No stale ownership property");
            DestroyWindow(target);
        }
        // Integration test uses only an isolated fixture, never the user's Terminal settings.
        const auto fixture = data_directory() / "terminal-test-fixture.jsonc";
        write_atomic(
            fixture,
            "{ // preserve me\n\"profiles\":{\"defaults\":{\"font\":{\"face\":\"Example\"}},\"list\":[]}}\n");
        auto p = preview_terminal(fixture, "", 85);
        check(parse_json(p.edited, true)["profiles"]["defaults"]["opacity"].asInt() == 85,
              "Terminal preview");
        check(p.edited.find("// preserve me") != std::string::npos, "Terminal comment preserved");
        apply_terminal(p);
        auto external = edit_jsonc(read_file(fixture), {"profiles", "defaults", "opacity"}, Json::Value(90));
        external =
            edit_jsonc(external, {"profiles", "defaults", "font", "face"}, Json::Value("User changed font"));
        write_atomic(fixture, external);
        reset_terminal();
        auto reset = parse_json(read_file(fixture), true);
        check(reset["profiles"]["defaults"]["opacity"].asInt() == 90,
              "Reset preserves external opacity edit");
        check(!reset["profiles"]["defaults"].isMember("useAcrylic"), "Reset removes owned Acrylic field");
        check(reset["profiles"]["defaults"]["font"]["face"].asString() == "User changed font",
              "Reset preserves unrelated user edit");
        write_atomic(fixture, "{\"profiles\":{\"list\":[]}}\n");
        auto inactive = preview_terminal(fixture, "", 85, 95);
        apply_terminal(inactive);
        check(parse_json(read_file(fixture), true)["profiles"]["defaults"]["unfocusedAppearance"]["opacity"]
                      .asInt() == 95,
              "Inactive Terminal appearance");
        reset_terminal();
        check(!parse_json(read_file(fixture), true)["profiles"].isMember("defaults"),
              "Reset removes only created empty containers");
        write_atomic(fixture, "{\"profiles\":{\"list\":[{\"guid\":\"one\"},{\"guid\":\"two\"}]}}\n");
        auto profile = preview_terminal(fixture, "one", 85);
        apply_terminal(profile);
        auto reordered = parse_json(read_file(fixture), true);
        auto first = reordered["profiles"]["list"][0];
        reordered["profiles"]["list"][0] = reordered["profiles"]["list"][1];
        reordered["profiles"]["list"][1] = first;
        write_atomic(fixture, stringify(reordered));
        reset_terminal();
        auto profiles = parse_json(read_file(fixture), true);
        check(!profiles["profiles"]["list"][1].isMember("opacity"),
              "Reset resolves reordered profile by GUID");
        std::filesystem::remove(fixture);
        std::cout << "Windows state and Terminal fixture tests passed\n";
    } catch (const std::exception &e) {
        if (argc > 1) {
            try {
                wfx::write_atomic(wfx::data_directory() / "controller-probe-error.txt", e.what());
            } catch (...) {
            }
        }
        std::cerr << e.what() << '\n';
        return 1;
    }
}
