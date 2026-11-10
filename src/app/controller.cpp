#include "app/controller.hpp"
#include "app/control_ids.hpp"
#include "app/theme.hpp"
#include "app/web_host.hpp"
#include "core/config.hpp"
#include "integrations/terminal.hpp"
#include "platform/focus.hpp"
#include "platform/mouse.hpp"
#include "platform/overlays.hpp"
#include "platform/taskbar.hpp"
#include <algorithm>
#include <commdlg.h>
#include <dwmapi.h>
#include <map>
#include <sddl.h>
#include <set>
#include <shellapi.h>
#include <uxtheme.h>

namespace wfx::ui {
namespace {
constexpr UINT event_message = WM_APP + 1, tray_message = WM_APP + 2, command_message = WM_APP + 3;
HWND event_target{};
void CALLBACK event_callback(HWINEVENTHOOK, DWORD event, HWND h, LONG object, LONG child, DWORD, DWORD) {
    if (!h || (event >= EVENT_OBJECT_CREATE && (object != OBJID_WINDOW || child != CHILDID_SELF)))
        return;
    if (event_target)
        PostMessageW(event_target, event_message, event, reinterpret_cast<LPARAM>(h));
}
std::wstring text(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}
struct Control {
    HWND handle{};
    int page{};
    RECT bounds{};
    COLORREF background{};
    bool small{};
    int background_role = 2;
};
struct Override {
    Identity identity;
    std::optional<Appearance> appearance;
    std::optional<Appearance> resume;
};
class Controller {
  public:
    HINSTANCE instance{};
    HWND hwnd{};
    unsigned dpi = 96;
    int page = 0;
    Theme theme;
    WebHost web;
    bool web_active = false;
    bool web_loading = false, web_failed = false, was_minimized = false;
    std::string web_page = "global";
    bool native_only = false;
    HANDLE taskbar_setup_process{};
    Config config;
    Recovery recovery;
    Taskbar taskbars{recovery};
    Overlays overlays;
    MouseControl mouse;
    FocusSession focus{recovery};
    std::vector<Control> controls;
    std::vector<HWINEVENTHOOK> hooks;
    std::map<HWND, Window> known;
    std::vector<Window> listed;
    std::map<HWND, Override> overrides;
    std::set<HWND> dirty;
    std::optional<Window> selected;
    std::string editing_id;
    HWND preview_target{};
    Appearance preview_appearance;
    int selected_tint = 0, default_tint = 0, wheel_remainder = 0;
    HWND wheel_target{};
    HWND focus_suspend_target{};
    bool paused = false, blocked = false, read_only = false, geometry_pending = false, recording = false,
         list_pending = false, focus_suspended = false;
    bool test_instance = false;
    DWORD test_target_pid{};
    unsigned taskbar_created{};
    HWND tooltip{};
    HWND control(int id) const {
        return GetDlgItem(hwnd, id);
    }
    void apply_ui_theme() {
        theme.set_light(config.ui_theme == "light", config.ui_theme);
        for (auto &c : controls) {
            c.background = c.background_role == 0 ? canvas : c.background_role == 1 ? sidebar : card;
            theme.apply(c.handle, c.background);
            if (c.small)
                SendMessageW(c.handle, WM_SETFONT, reinterpret_cast<WPARAM>(theme.small), TRUE);
        }
        BOOL dark = config.ui_theme != "light";
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(hwnd, 35, &canvas, sizeof(canvas));
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    void show_web() {
        if (native_only || (test_instance && !GetEnvironmentVariableW(L"GLASSIFY_WEB_TEST", nullptr, 0)))
            return;
        KillTimer(hwnd, 11);
        if (web_failed || ((!IsWindowVisible(hwnd) || IsIconic(hwnd)) && !config.keep_interface_ready))
            return;
        if (web.started()) {
            web.resize();
            publish_web();
            return;
        }
        web_loading = true;
        for (const auto &c : controls)
            ShowWindow(c.handle, SW_HIDE);
        InvalidateRect(hwnd, nullptr, TRUE);
        SetTimer(hwnd, 15, 12000, nullptr);
        const bool started = web.start(
            hwnd, [this](const std::string &message) { web_message(message); },
            [this](bool ok) {
                if (!ok)
                    KillTimer(hwnd, 15);
                web_loading = ok;
                web_failed = !ok;
                web_active = false;
                switch_page(page);
                if (ok) {
                    update_list();
                    publish_web();
                } else {
                    say("The interface could not load. Native controls are available. Choose Reload "
                        "interface to try again.");
                    SetTimer(hwnd, 14, 1, nullptr);
                }
            },
            config.ui_theme, config.ui_style);
        if (!started) {
            KillTimer(hwnd, 15);
            web_loading = false;
            web_failed = true;
            web_active = false;
            switch_page(page);
            say("The styled interface is unavailable. Use these controls or choose Reload interface.");
        }
    }
    void publish_web() {
        if (!web.ready())
            return;
        if (!IsWindowVisible(hwnd) && web_active)
            return;
        Json::Value value;
        value["type"] = "state";
        value["interfaceReady"] = web_active;
        value["previewActive"] = preview_target != nullptr;
        value["page"] = web_page;
        value["config"] = parse_json(serialize_config(config));
        value["paused"] = paused;
        value["blocked"] = blocked;
        value["startup"] = checked(startup_check);
        value["status"] = utf8(text(control(status_label)));
        value["pauseShortcut"] = utf8(shortcut_name());
        value["taskbarInstalled"] = Taskbar::installed();
        value["taskbarStatus"] = utf8(text(control(taskbar_status)));
        value["taskbarSetupRunning"] = taskbar_setup_process != nullptr;
        value["focusStatus"] = focus_suspended          ? "SUSPENDED"
                               : focus.target()         ? "ACTIVE"
                               : config.focus_maximized ? "READY"
                                                        : "OFF";
        value["windows"] = Json::Value(Json::arrayValue);
        for (const auto &[h, w] : known) {
            if (!same_identity(w.identity) || !IsWindowVisible(h) || IsIconic(h))
                continue;
            Json::Value item;
            item["id"] = std::to_string(reinterpret_cast<ULONG_PTR>(h));
            item["title"] = utf8(w.title);
            item["app"] = utf8(std::filesystem::path(wide(w.match.executable)).filename().wstring());
            value["windows"].append(item);
        }
        value["rules"] = Json::Value(Json::arrayValue);
        for (const auto &r : config.rules) {
            Json::Value item;
            item["id"] = r.id;
            item["app"] = utf8(std::filesystem::path(wide(r.executable)).filename().wstring());
            item["enabled"] = r.enabled;
            item["exclude"] = r.exclude;
            item["active"] = r.appearance.active;
            item["inactive"] = r.appearance.inactive;
            value["rules"].append(item);
        }
        auto &e = value["editor"];
        e["key"] = selected && same_identity(selected->identity)
                       ? std::to_string(reinterpret_cast<ULONG_PTR>(selected->identity.hwnd))
                       : "";
        e["ruleId"] = editing_id;
        e["path"] = utf8(text(control(path_edit)));
        e["name"] = utf8(text(control(selected_name)));
        e["active"] = _wtoi(text(control(active_edit)).c_str());
        e["inactive"] = _wtoi(text(control(inactive_edit)).c_str());
        e["strength"] = _wtoi(text(control(tint_strength_edit)).c_str());
        e["tint"] = selected_tint;
        e["enabled"] = checked(enabled_check);
        e["exclude"] = checked(exclude_check);
        value["presets"] = Json::Value(Json::arrayValue);
        for (const auto &p : presets()) {
            Json::Value item;
            item["id"] = p.id;
            item["name"] = p.name;
            item["description"] = p.description;
            item["active"] = p.appearance.active;
            item["inactive"] = p.appearance.inactive;
            item["tint"] = p.appearance.tint;
            value["presets"].append(item);
        }
        web.send(stringify(value));
    }
    void set_web_editor(const Json::Value &e) {
        if (!e.isObject() || !e["active"].isInt() || !e["inactive"].isInt() || !e["tint"].isInt() ||
            !e["strength"].isInt() || !e["enabled"].isBool() || !e["exclude"].isBool())
            throw std::runtime_error("Invalid editor values");
        validate(Appearance{Mode::opacity, e["active"].asInt(), e["inactive"].asInt(), e["tint"].asInt(),
                            e["strength"].asInt()});
        SetWindowTextW(control(active_edit), std::to_wstring(e["active"].asInt()).c_str());
        SetWindowTextW(control(inactive_edit), std::to_wstring(e["inactive"].asInt()).c_str());
        SetWindowTextW(control(tint_strength_edit), std::to_wstring(e["strength"].asInt()).c_str());
        checked(enabled_check, e["enabled"].asBool());
        checked(exclude_check, e["exclude"].asBool());
        selected_tint = e["tint"].asInt();
        sync_sliders();
    }
    void web_message(const std::string &message) {
        try {
            if (message.size() > 16384)
                throw std::runtime_error("UI message exceeds limit");
            const auto v = parse_json(message);
            if (!v.isObject() || !v["action"].isString())
                throw std::runtime_error("Invalid UI command");
            const auto action = v["action"].asString();
            if (action == "ready") {
                update_list();
            } else if (action == "interfaceReady") {
                if (!web.ready())
                    return;
                KillTimer(hwnd, 15);
                web_loading = false;
                web_active = true;
                switch_page(page);
                web.present();
            } else if (action == "page") {
                const auto p = v["page"].asString();
                if (p != "windows" && p != "presets" && p != "focus" && p != "automation" && p != "taskbar" &&
                    p != "settings" && p != "global")
                    throw std::runtime_error("Unknown UI page");
                web_page = p;
                switch_page(p == "windows"                                       ? 0
                            : p == "presets"                                     ? 1
                            : p == "automation" || p == "focus" || p == "global" ? 2
                            : p == "taskbar"                                     ? 4
                                                                                 : 3);
                if (p == "windows")
                    update_list();
            } else if (action == "selectWindow") {
                for (const auto &[h, w] : known)
                    if (std::to_string(reinterpret_cast<ULONG_PTR>(h)) == v["id"].asString() &&
                        same_identity(w.identity)) {
                        fill(w);
                        break;
                    }
            } else if (action == "selectRule") {
                for (size_t i = 0; i < config.rules.size(); ++i)
                    if (config.rules[i].id == v["id"].asString()) {
                        SendMessageW(control(rules_list), LB_SETCURSEL, i, 0);
                        edit_rule();
                        break;
                    }
            } else if (action == "theme") {
                const auto choice = v["theme"].asString();
                if (choice != "dark" && choice != "light" && choice != "vscode" && choice != "ember")
                    throw std::runtime_error("Unknown theme");
                change([&] { config.ui_theme = choice; });
                apply_ui_theme();
            } else if (action == "style") {
                const auto choice = v["style"].asString();
                if (choice != "minimal" && choice != "glass")
                    throw std::runtime_error("Unknown interface style");
                change([&] { config.ui_style = choice; });
                say("Interface style saved.");
            } else if (action == "openAuthor") {
                ShellExecuteW(hwnd, L"open", L"https://github.com/xaicat", nullptr, nullptr, SW_SHOWNORMAL);
            } else if (action == "preview" || action == "saveApp" || action == "keepWindow") {
                set_web_editor(v["editor"]);
                if (action == "preview")
                    preview();
                else if (action == "saveApp")
                    keep();
                else
                    keep_window();
            } else if (action == "preset") {
                if (!v["index"].isUInt() || v["index"].asUInt() >= presets().size())
                    throw std::runtime_error("Unknown preset");
                command(preset_first + v["index"].asUInt(), 0);
            } else if (action == "toggle") {
                const auto key = v["key"].asString();
                if (!v["value"].isBool())
                    throw std::runtime_error("Expected a toggle value");
                const int id = key == "autoApply"        ? auto_quick
                               : key == "mouseControl"   ? mouse_check
                               : key == "minimizeToTray" ? minimize_check
                               : key == "closeToTray"    ? close_check
                               : key == "powerSaving"    ? power_check
                               : key == "startup"        ? startup_check
                               : key == "focusMaximized" ? focus_check
                                                         : 0;
                if (id) {
                    checked(id, v["value"].asBool());
                    command(id, 0);
                } else if (key == "keepInterfaceReady") {
                    change([&] { config.keep_interface_ready = v["value"].asBool(); });
                    say(config.keep_interface_ready
                            ? "Instant tray reopening enabled."
                            : "Low-memory tray mode enabled. Reopening may take a moment.");
                } else if (key == "globalTransparency") {
                    change([&] { config.global_transparency = v["value"].asBool(); });
                    overrides.clear();
                    cancel_preview();
                    reapply();
                    say(config.global_transparency
                            ? "Global transparency is on for eligible apps. Excluded and disabled apps stay "
                              "unchanged."
                            : "Global transparency is off. Saved app settings still apply.");
                } else if (key == "taskbarEnabled") {
                    if (v["value"].asBool() && !Taskbar::installed())
                        throw std::runtime_error("Set up the optional taskbar engine first.");
                    change([&] { config.taskbar_enabled = v["value"].asBool(); });
                    update_taskbar();
                } else
                    throw std::runtime_error("Unknown preference");
            } else if (action == "focusSetting") {
                const auto key = v["key"].asString();
                change([&] {
                    if (key == "focusDelayMs" && v["value"].isInt())
                        config.focus_delay_ms = v["value"].asInt();
                    else if (v["value"].isBool() && key == "focusFullscreenOnly")
                        config.focus_fullscreen_only = v["value"].asBool();
                    else if (v["value"].isBool() && key == "focusSameMonitor")
                        config.focus_same_monitor = v["value"].asBool();
                    else if (v["value"].isBool() && key == "focusRestoreOnSwitch")
                        config.focus_restore_on_switch = v["value"].asBool();
                    else
                        throw std::runtime_error("Unknown focus setting");
                });
                focus.stop();
                focus_suspended = false;
                update_focus();
                say("Focus behavior updated.");
            } else if (action == "saveFocus") {
                if (!v["exclusions"].isArray() || v["exclusions"].size() > 64)
                    throw std::runtime_error("Expected at most 64 executable filenames");
                std::vector<std::string> exclusions;
                for (const auto &item : v["exclusions"]) {
                    if (!item.isString())
                        throw std::runtime_error("Expected executable filename");
                    exclusions.push_back(item.asString());
                }
                change([&] { config.focus_exclusions = exclusions; });
                focus.stop();
                update_focus();
                say("Focus exclusions saved.");
            } else if (action == "saveDefaults") {
                for (const auto key : {"active", "inactive", "strength", "tint"})
                    if (!v[key].isInt())
                        throw std::runtime_error("Expected numeric defaults");
                change([&] {
                    config.defaults = Appearance{Mode::opacity, v["active"].asInt(), v["inactive"].asInt(),
                                                 v["tint"].asInt(), v["strength"].asInt()};
                    config.preset_id = "custom";
                });
                reapply();
                say("Global opacity and automatic defaults saved.");
            } else if (action == "applyTaskbar") {
                if (!v["opacity"].isInt() || v["opacity"].asInt() < 0 || v["opacity"].asInt() > 100)
                    throw std::runtime_error("Background opacity must be from 0 to 100");
                change([&] { config.taskbar_opacity = v["opacity"].asInt(); });
                update_taskbar();
                say("Taskbar background settings updated.");
            } else if (action == "setupTaskbar") {
                if (!taskbar_setup_process) {
                    const auto script = executable_path().parent_path() / "scripts" / "setup-taskbar.ps1";
                    wchar_t system[32768]{};
                    GetSystemDirectoryW(system, 32768);
                    const auto powershell =
                        std::filesystem::path(system) / "WindowsPowerShell" / "v1.0" / "powershell.exe";
                    auto cmd = L"\"" + powershell.wstring() +
                               L"\" -NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() +
                               L"\" -DataDirectory \"" + data_directory().wstring() + L"\"";
                    STARTUPINFOW startup{};
                    startup.cb = sizeof(startup);
                    PROCESS_INFORMATION child{};
                    if (!std::filesystem::exists(script) ||
                        !CreateProcessW(powershell.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child))
                        throw std::runtime_error("Could not start taskbar engine setup");
                    CloseHandle(child.hThread);
                    taskbar_setup_process = child.hProcess;
                    SetTimer(hwnd, 13, 250, nullptr);
                    say("Setting up taskbar transparency. This may take a moment.");
                }
            } else {
                const std::map<std::string, int> actions{{"refresh", refresh},
                                                         {"pick", picker},
                                                         {"deleteRule", delete_button},
                                                         {"undo", cancel_button},
                                                         {"pause", pause_button},
                                                         {"hide", hide_button},
                                                         {"restore", restore_button},
                                                         {"restoreFocus", restore_focus_button},
                                                         {"import", import_button},
                                                         {"export", export_button},
                                                         {"dataFolder", data_button},
                                                         {"quit", quit_button},
                                                         {"recordShortcut", hotkey_save},
                                                         {"resetTaskbar", taskbar_reset}};
                const auto found = actions.find(action);
                if (found == actions.end())
                    throw std::runtime_error("Unknown UI action");
                command(found->second, 0);
            }
        } catch (const std::exception &e) {
            failure(e);
            Json::Value notice;
            notice["type"] = "notice";
            notice["message"] = e.what();
            web.send(stringify(notice));
        }
        if (IsWindow(hwnd))
            publish_web();
    }
    int scale(int value) const {
        return MulDiv(value, dpi, 96);
    }
    int number(int id, int lo, int hi, const char *name) const {
        const auto value = text(control(id));
        try {
            size_t end{};
            const int n = std::stoi(value, &end);
            if (end == value.size() && n >= lo && n <= hi)
                return n;
        } catch (const std::exception &) {
        }
        throw std::runtime_error(std::string(name) + " must be a whole number from " + std::to_string(lo) +
                                 " to " + std::to_string(hi) + ".");
    }
    RECT rect(int x, int y, int w, int h) const {
        return {scale(x), scale(y), scale(x + w), scale(y + h)};
    }
    void say(const std::string &s) {
        SetWindowTextW(control(status_label), wide(s).c_str());
        if (web.ready())
            SetTimer(hwnd, 10, 40, nullptr);
    }
    void failure(const std::exception &e) {
        say(e.what());
        log(e.what());
    }
    bool checked(int id) const {
        return SendMessageW(control(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void checked(int id, bool state) {
        SendMessageW(control(id), BM_SETCHECK, state ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(control(id), nullptr, TRUE);
    }
    HWND add(const wchar_t *cls, const std::wstring &value, DWORD style, int id, int x, int y, int w, int h,
             int p, COLORREF bg = card, bool small = false) {
        auto c = CreateWindowExW(0, cls, value.c_str(), WS_CHILD | WS_VISIBLE | style, scale(x), scale(y),
                                 scale(w), scale(h), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 instance, nullptr);
        theme.apply(c, bg);
        if (small)
            SetPropW(c, L"WFX.Muted", reinterpret_cast<HANDLE>(1));
        if (small)
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(theme.small), TRUE);
        controls.push_back({c, p, {x, y, w, h}, bg, small, bg == canvas ? 0 : bg == sidebar ? 1 : 2});
        return c;
    }
    void label(const std::wstring &value, int x, int y, int w, int p, bool small = false, int h = 24,
               COLORREF bg = card, int id = -1) {
        add(L"STATIC", value, SS_LEFT, id, x, y, w, h, p, bg, small);
    }
    void button(const std::wstring &value, int id, int x, int y, int w, int p, int h = 36,
                COLORREF bg = card) {
        add(L"BUTTON", value, WS_TABSTOP | BS_OWNERDRAW, id, x, y, w, h, p, bg);
    }
    void check(const std::wstring &value, int id, int x, int y, int w, int p) {
        add(L"BUTTON", value, WS_TABSTOP | BS_AUTOCHECKBOX, id, x, y, w, 28, p);
    }
    void edit(const std::wstring &value, int id, int x, int y, int w, int p, bool readonly = false) {
        add(L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL | (readonly ? ES_READONLY : ES_NUMBER), id, x, y, w,
            28, p, canvas);
    }
    void slider(int id, int x, int y, int w, int p, int lo = 30, int hi = 100) {
        auto h = add(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_NOTICKS, id, x, y, w, 28, p);
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    }
    void initialize() {
        event_target = hwnd;
        theme.initialize(dpi);
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        COLORREF edge = border;
        DwmSetWindowAttribute(hwnd, 34, &edge, sizeof(edge));
        wchar_t test[128]{};
        test_instance = GetEnvironmentVariableW(L"WINFADEX_TEST_INSTANCE", test, 128) != 0;
        wchar_t target_pid[32]{};
        if (test_instance && GetEnvironmentVariableW(L"WINFADEX_TEST_TARGET_PID", target_pid, 32))
            test_target_pid = wcstoul(target_pid, nullptr, 10);
        try {
            auto path = data_directory() / "config.json";
            if (std::filesystem::exists(path))
                config = parse_config(read_file(path));
        } catch (const std::exception &e) {
            read_only = true;
            log(e.what());
        }
        const wchar_t *names[]{L"Windows", L"Presets", L"Automation", L"Settings", L"Taskbar"};
        for (int i = 0; i < 5; ++i)
            button(names[i], nav_first + i, 16, 138 + i * 54, 152, -1, 42, sidebar);
        button(L"Hide to tray", hide_button, 940, 30, 136, -1, 36, canvas);
        button(L"Reload interface", 709, 16, 594, 152, -1, 34, sidebar);
        check(L"Global mode", 710, 16, 548, 152, -1);
        button(L"Pause", pause_button, 816, 30, 112, -1, 36, canvas);
        check(L"Auto apply", auto_quick, 600, 34, 190, -1);
        theme.apply(control(auto_quick), canvas);
        controls.back().background_role = 0;
        // Windows page.
        label(L"Open windows", 238, 138, 280, 0);
        add(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, windows_list, 238,
            174, 286, 214, 0);
        label(L"Select a window to change its appearance.", 238, 394, 286, 0, true, 32);
        label(L"Saved app settings", 238, 436, 286, 0);
        add(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, rules_list, 238,
            470, 286, 102, 0);
        button(L"Pick a window", picker, 238, 590, 138, 0);
        button(L"Refresh list", refresh, 386, 590, 138, 0);
        label(L"Choose a window", 590, 138, 460, 0, false, 24, card, selected_name);
        edit(L"", path_edit, 590, 174, 460, 0, true);
        label(L"Opacity while focused", 590, 222, 440, 0);
        label(L"Used when this window is selected.", 590, 248, 440, 0, true);
        slider(active_slider, 590, 278, 350, 0);
        edit(L"100", active_edit, 966, 276, 84, 0);
        label(L"Opacity in the background", 590, 326, 440, 0);
        label(L"Used when another window is selected.", 590, 352, 440, 0, true);
        slider(inactive_slider, 590, 382, 350, 0);
        edit(L"95", inactive_edit, 966, 380, 84, 0);
        label(L"Color tint", 590, 426, 440, 0);
        for (int i = 0; i < 8; ++i)
            button(tint_name(i), tint_first + i, 590 + i * 57, 460, 50, 0, 32);
        label(L"Tint strength (0 to 40%)", 590, 514, 228, 0, true);
        slider(tint_strength_slider, 820, 510, 160, 0, 0, 40);
        edit(L"12", tint_strength_edit, 992, 510, 58, 0);
        check(L"Enable transparency for this app", enabled_check, 590, 556, 460, 0);
        check(L"Always keep this app opaque", exclude_check, 590, 596, 460, 0);
        button(L"Preview 15s", preview_button, 566, 656, 108, 0);
        button(L"Undo", cancel_button, 684, 656, 80, 0);
        button(L"Keep this window", keep_window_button, 774, 656, 132, 0);
        button(L"Save for this app", keep_button, 916, 656, 160, 0);
        button(L"Remove saved settings", delete_button, 238, 656, 286, 0);
        // Presets page.
        label(L"One click applies a preset to managed apps and the automatic default.", 238, 136, 810, 1,
              true, 32);
        for (size_t i = 0; i < presets().size(); ++i) {
            const auto &p = presets()[i];
            button(wide(p.name) + L"\n" + wide(p.description), preset_first + static_cast<int>(i),
                   238 + static_cast<int>(i % 3) * 274, 184 + static_cast<int>(i / 3) * 110, 260, 1, 94);
        }
        button(L"Previous preset", previous_preset, 238, 656, 190, 1);
        button(L"Next preset", next_preset, 442, 656, 190, 1);
        label(L"All presets change the whole window, including text. Tints add color, not blur.", 650, 656,
              402, 1, true, 38, canvas);
        // Automation page.
        check(L"Apply defaults to other apps", auto_apply_check, 238, 150, 370, 2);
        label(L"Apps without saved settings use the defaults.\nSaved exclusions still stay opaque.", 238, 184,
              370, 2, true, 42);
        check(L"Adjust with Ctrl+Alt+mouse wheel", mouse_check, 238, 248, 370, 2);
        label(L"Point at a window and scroll. This creates a\ntemporary override for that window only.", 238,
              282, 370, 2, true, 42);
        check(L"Focus maximized or full-screen apps", focus_check, 238, 346, 370, 2);
        label(L"Minimize other windows on the same screen.\nRestore them when the focus session ends.", 238,
              380, 370, 2, true, 42);
        check(L"Minimize the app to the tray", minimize_check, 238, 444, 370, 2);
        label(L"The title bar minimize button hides this app.\nDouble-click the tray icon to return.", 238,
              478, 370, 2, true, 42);
        check(L"Close the app to the tray", close_check, 238, 542, 370, 2);
        label(L"The close button keeps the app running.\nChoose Quit in the tray menu to stop it.", 238, 576,
              370, 2, true, 42);
        label(L"Automatic appearance", 674, 150, 370, 2);
        label(L"Focused opacity (%)", 674, 196, 240, 2);
        edit(L"95", default_active, 962, 192, 84, 2);
        label(L"Background opacity (%)", 674, 256, 260, 2);
        edit(L"85", default_inactive, 962, 252, 84, 2);
        label(L"Default tint", 674, 324, 370, 2);
        for (int i = 0; i < 8; ++i)
            button(tint_name(i), default_tint_first + i, 674 + (i % 4) * 94, 366 + (i / 4) * 44, 84, 2, 34);
        label(L"Tint strength (%)", 674, 476, 240, 2);
        edit(L"12", default_tint_strength, 962, 472, 84, 2);
        label(L"Lower opacity means more transparency.\n30% is the minimum. 100% is fully opaque.\nColor "
              L"tint affects text and content together.",
              674, 532, 372, 2, true, 70);
        button(L"Save automatic settings", save_automation, 674, 656, 372, 2);
        // Taskbar page. Explicit opt-in, independently restored from ordinary app rules.
        check(L"Enable taskbar transparency", taskbar_check, 238, 150, 780, 4);
        label(L"Taskbar opacity", 238, 212, 780, 4);
        slider(taskbar_slider, 238, 254, 660, 4, 0, 100);
        edit(L"85", taskbar_edit, 962, 254, 84, 4);
        label(L"0% is clear. 100% is solid. Icons and labels stay fully opaque.", 238, 312, 790, 4, true, 48);
        button(L"Apply taskbar settings", taskbar_save, 238, 390, 360, 4);
        button(L"Turn off and restore", taskbar_reset, 618, 390, 428, 4);
        button(L"Set up taskbar engine", 708, 238, 444, 360, 4);
        label(L"Disabled by default. Changes restore when effects pause or Glassify exits.", 238, 494, 790, 4,
              true, 48);
        label(L"Experimental: Explorer rendering differs between Windows builds. Check the visible result.\n"
              L"Use one taskbar customization app at a time.",
              238, 548, 790, 4, true, 50);
        label(L"Taskbar controls are ready.", 238, 610, 790, 4, true, 38, card, taskbar_status);
        // Settings page.
        label(L"Keyboard shortcuts", 238, 142, 780, 3);
        label(L"Ctrl+Alt+Up / Down\nCtrl+Alt+T\nCtrl+Alt+Left / Right\nCtrl+Alt+H\nCtrl+Alt+M", 238, 192, 264,
              3, false, 162);
        label(L"Adjust the focused window by 5%\nToggle transparency for that window\nSwitch between "
              L"presets\nShow or hide the control panel\nRestore windows minimized by focus mode",
              526, 192, 520, 3, false, 162);
        button(L"Change pause shortcut", hotkey_save, 238, 382, 286, 3);
        label(L"", 550, 388, 494, 3, true, 30, card, hotkey_key);
        check(L"Respect Windows power-saving mode", power_check, 238, 450, 800, 3);
        check(L"Start Glassify when I sign in", startup_check, 238, 498, 800, 3);
        button(L"Open data folder", data_button, 238, 560, 244, 3);
        button(L"Import settings", import_button, 496, 560, 250, 3);
        button(L"Export settings", export_button, 760, 560, 286, 3);
        button(L"Restore all windows", restore_button, 238, 616, 244, 3);
        button(L"Restore focus windows", restore_focus_button, 496, 616, 250, 3);
        button(L"Quit and restore", quit_button, 760, 616, 286, 3);
        button(L"Restore Terminal settings", terminal_reset, 238, 670, 350, 3, 28, canvas);
        button(L"Switch light / dark theme", 707, 760, 670, 286, 3, 28, canvas);
        label(L"Ready. Select a window or turn on Auto apply.", 214, 718, 862, -1, true, 32, canvas,
              status_label);
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                  WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                                  CW_USEDEFAULT, CW_USEDEFAULT, hwnd, nullptr, instance, nullptr);
        SetWindowTheme(tooltip, L"", L"");
        SendMessageW(tooltip, TTM_SETTIPBKCOLOR, canvas, 0);
        SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, text_color, 0);
        auto tip = [&](int id, const wchar_t *value) {
            TOOLINFOW info{};
            info.cbSize = sizeof(info);
            info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            info.hwnd = hwnd;
            info.uId = reinterpret_cast<UINT_PTR>(control(id));
            info.lpszText = const_cast<PWSTR>(value);
            SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        };
        for (int i = 0; i < 8; ++i) {
            tip(tint_first + i, tint_name(i));
            tip(default_tint_first + i, tint_name(i));
        }
        tip(preview_button,
            L"Try the current values for 15 seconds, then return to the previous appearance.");
        tip(keep_window_button,
            L"Keep these values for this window only until it closes, you quit, or you switch presets.");
        tip(keep_button,
            L"Save these values for this app, including future windows from the same executable.");
        sync_settings();
        apply_ui_theme();
        checked(enabled_check, true);
        sync_sliders();
        if (!recovery.start()) {
            paused = true;
            SetWindowTextW(control(pause_button), L"Resume");
            say("Recovery could not start. Window changes are disabled.");
        }
        blocked = policy_blocks(config.power_saving);
        refresh_windows();
        register_keys();
        if (!test_instance && !mouse.start(hwnd))
            say("Mouse control could not start. Keyboard controls still work.");
        mouse.set_enabled(config.mouse_control && !paused && !blocked);
        hooks.push_back(SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                        event_callback, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        hooks.push_back(SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, event_callback, 0,
                                        0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        hooks.push_back(SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr,
                                        event_callback, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        hooks.push_back(SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr,
                                        event_callback, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS));
        if (std::any_of(hooks.begin(), hooks.end(), [](auto h) { return !h; })) {
            paused = true;
            restore_all();
            say("Window tracking could not start. Effects are paused.");
        }
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        tray(true);
        update_taskbar();
        SetTimer(hwnd, 5, 1500, nullptr);
        if (config.keep_interface_ready)
            SetTimer(hwnd, 16, 1, nullptr);
        switch_page(0);
        if (read_only)
            say("The settings file could not be read. Import a valid file to recover; the original is "
                "unchanged.");
    }
    void sync_settings() {
        checked(taskbar_check, config.taskbar_enabled);
        SetWindowTextW(control(taskbar_edit), std::to_wstring(config.taskbar_opacity).c_str());
        SendMessageW(control(taskbar_slider), TBM_SETPOS, TRUE, config.taskbar_opacity);
        checked(auto_quick, config.auto_apply);
        checked(auto_apply_check, config.auto_apply);
        checked(710, config.global_transparency);
        checked(mouse_check, config.mouse_control);
        checked(focus_check, config.focus_maximized);
        checked(minimize_check, config.minimize_to_tray);
        checked(close_check, config.close_to_tray);
        checked(power_check, config.power_saving);
        SetWindowTextW(control(default_active), std::to_wstring(config.defaults.active).c_str());
        SetWindowTextW(control(default_inactive), std::to_wstring(config.defaults.inactive).c_str());
        SetWindowTextW(control(default_tint_strength),
                       std::to_wstring(config.defaults.tint_strength).c_str());
        default_tint = config.defaults.tint;
        DWORD size = 32768 * sizeof(wchar_t);
        std::wstring value(32768, 0);
        checked(startup_check,
                RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                             L"Glassify", RRF_RT_REG_SZ, nullptr, value.data(), &size) == ERROR_SUCCESS);
        const auto shortcut = shortcut_name();
        SetWindowTextW(control(hotkey_key), (L"Pause: " + shortcut).c_str());
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    std::wstring shortcut_name() const {
        std::wstring s;
        if (config.hotkey_modifiers & MOD_CONTROL)
            s += L"Ctrl+";
        if (config.hotkey_modifiers & MOD_ALT)
            s += L"Alt+";
        if (config.hotkey_modifiers & MOD_SHIFT)
            s += L"Shift+";
        if (config.hotkey_modifiers & MOD_WIN)
            s += L"Win+";
        wchar_t name[64]{};
        GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(config.hotkey_key, MAPVK_VK_TO_VSC) << 16), name,
                        64);
        return s + name;
    }
    void save() {
        if (read_only)
            throw std::runtime_error("Settings are read-only after a load error. Import a valid file first.");
        write_atomic(data_directory() / "config.json", serialize_config(config));
    }
    template <typename F> void change(F mutation) {
        auto old = config;
        mutation();
        try {
            save();
        } catch (...) {
            config = old;
            sync_settings();
            throw;
        }
        sync_settings();
    }
    void sync_sliders() {
        for (const auto pair :
             std::initializer_list<std::pair<int, int>>{{active_edit, active_slider},
                                                        {inactive_edit, inactive_slider},
                                                        {tint_strength_edit, tint_strength_slider}}) {
            auto s = text(control(pair.first));
            if (!s.empty())
                SendMessageW(control(pair.second), TBM_SETPOS, TRUE, _wtoi(s.c_str()));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void tray(bool add) {
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof(n);
        n.hWnd = hwnd;
        n.uID = 1;
        n.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
        n.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        n.uCallbackMessage = tray_message;
        wcscpy_s(n.szTip, L"Glassify - opacity and focus controls");
        Shell_NotifyIconW(add ? NIM_ADD : NIM_DELETE, &n);
    }
    void switch_page(int value) {
        page = std::clamp(value, 0, 4);
        for (const auto &c : controls)
            ShowWindow(c.handle,
                       !web_active && !web_loading && (c.page < 0 || c.page == page) ? SW_SHOW : SW_HIDE);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    void paint(HDC dc) {
        RECT all{};
        GetClientRect(hwnd, &all);
        FillRect(dc, &all, theme.canvas_brush);
        if (web_loading) {
            draw_text(dc, theme.title, L"Glassify", rect(32, 28, 300, 44), text_color);
            draw_text(dc, theme.body, L"Opening your workspace...", rect(32, 80, 600, 30), muted);
            for (int i = 0; i < 4; ++i)
                rounded(dc, rect(32, 148 + i * 56, 700 - i * 80, 20), card, card, 8);
            return;
        }
        if (web_active)
            return;
        RECT side = rect(0, 0, 184, 760);
        FillRect(dc, &side, theme.sidebar_brush);
        DrawIconEx(dc, scale(20), scale(31), LoadIconW(instance, MAKEINTRESOURCEW(101)), scale(32), scale(32),
                   0, nullptr, DI_NORMAL);
        draw_text(dc, theme.brand, L"Glassify", rect(62, 30, 112, 36), text_color);
        draw_text(dc, theme.small, L"WINDOW CONTROL", rect(20, 72, 150, 22), muted);
        draw_text(dc, theme.small, L"v1.0.0\nBy xaicat\nNative window control", rect(20, 660, 150, 55), muted,
                  DT_LEFT | DT_WORDBREAK);
        const wchar_t *titles[]{L"Your windows", L"Find your balance", L"Make it automatic",
                                L"Stay in control", L"A lighter taskbar"};
        const wchar_t *descriptions[]{L"Set opacity and color for each app. Text fades with the window.",
                                      L"Choose one of twelve looks. You can fine-tune each app afterwards.",
                                      L"Decide what happens when apps open, move, or become maximized.",
                                      L"Keyboard controls, startup preferences, and recovery tools.",
                                      L"Optional background-only opacity. Icons and labels stay solid."};
        draw_text(dc, theme.title, titles[page], rect(214, 26, 570, 36), text_color);
        draw_text(dc, theme.small, descriptions[page], rect(214, 70, 862, 26), muted);
        if (page == 0) {
            rounded(dc, rect(214, 116, 334, 526), card);
            rounded(dc, rect(566, 116, 510, 526), card);
        } else if (page == 2) {
            rounded(dc, rect(214, 116, 420, 526), card);
            rounded(dc, rect(650, 116, 426, 526), card);
        } else
            rounded(dc, rect(214, 116, 862, page == 1 ? 526 : 540), card);
        for (const auto &c : controls)
            if (c.page == page) {
                wchar_t cls[32]{};
                GetClassNameW(c.handle, cls, 32);
                if (wcscmp(cls, L"Edit") == 0)
                    rounded(
                        dc,
                        rect(c.bounds.left - 6, c.bounds.top - 4, c.bounds.right + 12, c.bounds.bottom + 8),
                        canvas, border, 8);
            }
    }
    bool draw_item(const DRAWITEMSTRUCT &d) {
        const auto id = static_cast<int>(d.CtlID);
        if ((id >= tint_first && id < tint_first + 8) ||
            (id >= default_tint_first && id < default_tint_first + 8)) {
            const bool defaults = id >= default_tint_first;
            const int color = id - (defaults ? default_tint_first : tint_first);
            const bool chosen = color == (defaults ? default_tint : selected_tint);
            rounded(d.hDC, d.rcItem, color ? tint_color(color) : canvas, chosen ? text_color : border, 10);
            if (!color)
                draw_text(d.hDC, theme.small, L"Off", d.rcItem, text_color,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return true;
        }
        bool chosen = id >= nav_first && id < nav_first + 5 ? page == id - nav_first : false;
        if (id >= preset_first && id < preset_first + static_cast<int>(presets().size()))
            chosen = config.preset_id == presets()[id - preset_first].id;
        theme.draw_button(d, chosen, id == keep_button || id == save_automation);
        return true;
    }
    void refresh_rules() {
        SendMessageW(control(rules_list), LB_RESETCONTENT, 0, 0);
        for (const auto &r : config.rules) {
            auto name = std::filesystem::path(wide(r.executable)).filename().wstring();
            auto s = std::wstring(r.exclude ? L"Opaque: " : r.enabled ? L"" : L"Off: ") + name;
            const auto index =
                SendMessageW(control(rules_list), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
            if (r.id == editing_id)
                SendMessageW(control(rules_list), LB_SETCURSEL, index, 0);
        }
    }
    void update_list() {
        listed.clear();
        SendMessageW(control(windows_list), LB_RESETCONTENT, 0, 0);
        for (const auto &[h, w] : known) {
            if (!IsWindow(h) || !IsWindowVisible(h) || IsIconic(h))
                continue;
            listed.push_back(w);
            auto title = w.title.empty()
                             ? std::filesystem::path(wide(w.match.executable)).filename().wstring()
                             : w.title;
            SendMessageW(control(windows_list), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(title.c_str()));
            if (selected && selected->identity.hwnd == h)
                SendMessageW(control(windows_list), LB_SETCURSEL, listed.size() - 1, 0);
        }
        refresh_rules();
        if (web.ready())
            SetTimer(hwnd, 10, 40, nullptr);
    }
    void schedule_list() {
        if (!list_pending && page == 0 && IsWindowVisible(hwnd)) {
            list_pending = true;
            SetTimer(hwnd, 9, 120, nullptr);
        }
    }
    void refresh_windows() {
        cancel_preview();
        focus.stop();
        overlays.clear();
        recovery.restore_all();
        known.clear();
        for (auto w : enumerate())
            if (!test_target_pid || w.identity.pid == test_target_pid)
                known.emplace(w.identity.hwnd, std::move(w));
        update_list();
        reapply();
    }
    std::optional<Appearance> appearance(const Window &w) {
        if (const auto r = match_rule(config.rules, w.match); r && r->exclude)
            return {};
        if (auto found = overrides.find(w.identity.hwnd); found != overrides.end()) {
            if (same_identity(found->second.identity))
                return found->second.appearance;
            overrides.erase(found);
        }
        return resolve_appearance(config, w.match);
    }
    void apply_window(HWND h) {
        auto found = known.find(h);
        if (found == known.end())
            return;
        if (paused || blocked || !recovery.alive() || !exclusion(h).empty()) {
            overlays.remove(h);
            recovery.restore(h);
            return;
        }
        auto a =
            h == preview_target ? std::optional<Appearance>(preview_appearance) : appearance(found->second);
        if (!a) {
            overlays.remove(h);
            recovery.restore(h);
            return;
        }
        const auto result = recovery.apply(found->second, *a, h == GetForegroundWindow());
        const bool accepted = result.starts_with("Whole-window opacity applied") ||
                              result.starts_with("Original non-layered") ||
                              result == "Applied (appearance unverified)";
        if (!accepted) {
            overlays.remove(h);
            say(result);
            return;
        }
        overlays.update(found->second, *a);
    }
    std::vector<Window> windows() const {
        std::vector<Window> r;
        for (const auto &[h, w] : known) {
            (void)h;
            r.push_back(w);
        }
        return r;
    }
    void update_focus() {
        if (!config.focus_maximized || paused || blocked || focus_suspended) {
            KillTimer(hwnd, 12);
            focus.stop();
            return;
        }
        // Foreground and geometry bursts settle before making a workspace change.
        SetTimer(hwnd, 12, config.focus_delay_ms, nullptr);
    }
    void apply_focus() {
        const auto foreground = GetForegroundWindow();
        DWORD pid{};
        GetWindowThreadProcessId(foreground, &pid);
        auto candidates = windows();
        std::erase_if(candidates, [&](const Window &w) {
            if (const auto r = match_rule(config.rules, w.match); r && r->exclude)
                return true;
            const auto filename = std::filesystem::path(wide(w.match.executable)).filename().wstring();
            return std::any_of(config.focus_exclusions.begin(), config.focus_exclusions.end(),
                               [&](const std::string &excluded) {
                                   return _wcsicmp(filename.c_str(), wide(excluded).c_str()) == 0;
                               });
        });
        bool owner_excluded = false;
        if (auto w = wfx::inspect(foreground, false, true)) {
            const auto rule = match_rule(config.rules, w->match);
            const auto filename = std::filesystem::path(wide(w->match.executable)).filename().wstring();
            owner_excluded = (rule && rule->exclude) ||
                             std::any_of(config.focus_exclusions.begin(), config.focus_exclusions.end(),
                                         [&](const std::string &excluded) {
                                             return _wcsicmp(filename.c_str(), wide(excluded).c_str()) == 0;
                                         });
        }
        focus.update(!test_target_pid || pid == test_target_pid ? foreground : nullptr, candidates,
                     config.focus_maximized && !paused && !blocked && !focus_suspended && !owner_excluded,
                     config.focus_fullscreen_only, config.focus_same_monitor, config.focus_restore_on_switch);
        if (web.ready())
            SetTimer(hwnd, 10, 40, nullptr);
    }
    std::optional<Window> eligible(HWND h) {
        auto w = wfx::inspect(h);
        if (w && test_target_pid && w->identity.pid != test_target_pid)
            return {};
        return w;
    }
    void reapply() {
        for (const auto &[h, w] : known) {
            (void)w;
            apply_window(h);
        }
        mouse.set_enabled(config.mouse_control && !paused && !blocked);
        update_focus();
        update_taskbar();
    }
    void update_taskbar() {
        if (test_instance || !config.taskbar_enabled || paused || blocked) {
            taskbars.restore();
            return;
        }
        const auto result = wide(taskbars.apply(config.taskbar_opacity));
        if (text(control(taskbar_status)) != result) {
            SetWindowTextW(control(taskbar_status), result.c_str());
            if (web.ready())
                SetTimer(hwnd, 10, 40, nullptr);
        }
    }
    void restore_all() {
        taskbars.restore();
        cancel_preview();
        overlays.clear();
        focus.stop();
        recovery.restore_all();
    }
    void fill(const Window &w) {
        cancel_preview();
        selected = w;
        editing_id.clear();
        Appearance a = appearance(w).value_or(config.defaults);
        bool enabled = true, excluded = false;
        auto candidates = config.rules;
        for (auto &r : candidates)
            r.enabled = true;
        if (const auto match = match_rule(candidates, w.match)) {
            const auto r = std::find_if(config.rules.begin(), config.rules.end(),
                                        [&](const auto &value) { return value.id == match->id; });
            editing_id = r->id;
            if (!overrides.contains(w.identity.hwnd))
                a = r->appearance;
            enabled = r->enabled;
            excluded = r->exclude;
        }
        if (auto found = overrides.find(w.identity.hwnd);
            found != overrides.end() && !found->second.appearance)
            enabled = false;
        SetWindowTextW(control(selected_name),
                       std::filesystem::path(wide(w.match.executable)).filename().c_str());
        SetWindowTextW(control(path_edit), wide(w.match.executable).c_str());
        SetWindowTextW(control(active_edit), std::to_wstring(a.active).c_str());
        SetWindowTextW(control(inactive_edit), std::to_wstring(a.inactive).c_str());
        SetWindowTextW(control(tint_strength_edit), std::to_wstring(a.tint_strength).c_str());
        selected_tint = a.tint;
        checked(enabled_check, enabled);
        checked(exclude_check, excluded);
        sync_sliders();
        say("Choose opacity or a tint. Try it first, then save for this app.");
    }
    void edit_rule() {
        const auto index = SendMessageW(control(rules_list), LB_GETCURSEL, 0, 0);
        if (index < 0 || static_cast<size_t>(index) >= config.rules.size())
            return;
        const auto r = config.rules[static_cast<size_t>(index)];
        cancel_preview();
        editing_id = r.id;
        selected.reset();
        for (const auto &[h, w] : known)
            if (w.match.executable == r.executable) {
                selected = w;
                break;
            }
        SetWindowTextW(control(path_edit), wide(r.executable).c_str());
        SetWindowTextW(control(selected_name), std::filesystem::path(wide(r.executable)).filename().c_str());
        SetWindowTextW(control(active_edit), std::to_wstring(r.appearance.active).c_str());
        SetWindowTextW(control(inactive_edit), std::to_wstring(r.appearance.inactive).c_str());
        SetWindowTextW(control(tint_strength_edit), std::to_wstring(r.appearance.tint_strength).c_str());
        selected_tint = r.appearance.tint;
        checked(enabled_check, r.enabled);
        checked(exclude_check, r.exclude);
        sync_sliders();
    }
    Rule editor() {
        Rule r;
        for (const auto &saved : config.rules)
            if (saved.id == editing_id)
                r = saved;
        r.id = editing_id.empty() ? std::to_string(GetTickCount64()) : editing_id;
        r.executable = utf8(text(control(path_edit)));
        if (r.executable.empty())
            throw std::runtime_error("Select a window from the list first.");
        r.filename_only = false;
        r.enabled = checked(enabled_check);
        r.exclude = checked(exclude_check);
        r.appearance = {Mode::opacity, number(active_edit, 30, 100, "Focused opacity"),
                        number(inactive_edit, 30, 100, "Background opacity"), selected_tint,
                        number(tint_strength_edit, 0, 40, "Tint strength")};
        validate(r);
        return r;
    }
    void cancel_preview() {
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 6);
        auto h = preview_target;
        preview_target = nullptr;
        if (h) {
            recovery.restore(h);
            overlays.remove(h);
            apply_window(h);
        }
    }
    void preview() {
        if (paused || blocked)
            throw std::runtime_error("Resume the app before previewing changes.");
        const auto r = editor();
        auto target =
            selected && same_identity(selected->identity) ? known.find(selected->identity.hwnd) : known.end();
        if (target == known.end()) {
            for (auto it = known.begin(); it != known.end(); ++it)
                if (it->second.match.executable == r.executable) {
                    target = it;
                    break;
                }
        }
        if (target == known.end())
            throw std::runtime_error("Open this app, then select its window to preview changes.");
        if (preview_target != target->first) {
            cancel_preview();
            preview_target = target->first;
            recovery.restore(preview_target);
        }
        preview_appearance = r.exclude || !r.enabled ? Appearance{Mode::opacity, 100, 100} : r.appearance;
        apply_window(preview_target);
        SetTimer(hwnd, 1, 15000, nullptr);
        say("Preview active for 15 seconds. Save to keep it, or undo to return to saved settings.");
    }
    void keep() {
        auto r = editor();
        change([&] {
            auto found = std::find_if(config.rules.begin(), config.rules.end(),
                                      [&](const auto &x) { return x.id == r.id; });
            if (found == config.rules.end())
                config.rules.push_back(r);
            else
                *found = r;
        });
        editing_id = r.id;
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 6);
        preview_target = nullptr;
        for (auto it = overrides.begin(); it != overrides.end();) {
            auto w = known.find(it->first);
            if (w != known.end() && w->second.match.executable == r.executable)
                it = overrides.erase(it);
            else
                ++it;
        }
        refresh_rules();
        reapply();
        say("Saved for this app. New windows from the same app will use these settings.");
    }
    void keep_window() {
        const auto r = editor();
        const WindowMatch match{r.executable, selected ? selected->match.window_class : "",
                                selected ? selected->match.title : ""};
        if (const auto existing = match_rule(config.rules, match);
            existing && existing->exclude && !r.exclude)
            throw std::runtime_error(
                "Remove this app's saved opaque setting before keeping a window override.");
        preview();
        const auto h = preview_target;
        const auto found = known.find(h);
        if (found == known.end())
            throw std::runtime_error("That window is no longer available.");
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 6);
        preview_target = nullptr;
        overrides[h] = {found->second.identity,
                        (r.exclude || !r.enabled) ? std::nullopt : std::optional<Appearance>(r.appearance),
                        {}};
        apply_window(h);
        say("Kept for this window only. It lasts until the window closes, you quit, or you switch presets.");
    }
    void apply_preset(int index) {
        index = (index + static_cast<int>(presets().size())) % static_cast<int>(presets().size());
        const auto &p = presets()[index];
        change([&] {
            config.defaults = p.appearance;
            config.preset_id = p.id;
            for (auto &r : config.rules)
                if (r.enabled && !r.exclude)
                    r.appearance = p.appearance;
        });
        overrides.clear();
        cancel_preview();
        reapply();
        refresh_rules();
        if (selected && same_identity(selected->identity))
            fill(*selected);
        say(p.name + " applied to managed apps and the automatic default.");
        InvalidateRect(hwnd, nullptr, TRUE);
    }
    void cycle_preset(int delta) {
        int index = 0;
        for (size_t i = 0; i < presets().size(); ++i)
            if (presets()[i].id == config.preset_id)
                index = static_cast<int>(i);
        apply_preset(index + delta);
    }
    void adjust(HWND h, int delta) {
        if (paused || blocked)
            return;
        auto w = eligible(h);
        if (!w)
            return;
        if (const auto r = match_rule(config.rules, w->match); r && r->exclude) {
            say("This app is excluded. Remove its opaque setting to adjust it.");
            return;
        }
        known[h] = *w;
        auto a = appearance(*w).value_or(config.defaults);
        const bool active = h == GetForegroundWindow();
        int &value = active ? a.active : a.inactive;
        value = std::clamp(value + delta, 30, 100);
        overrides[h] = {w->identity, a, {}};
        if (preview_target == h)
            cancel_preview();
        apply_window(h);
        if (selected && selected->identity.hwnd == h && same_identity(selected->identity))
            fill(*selected);
        say("Window opacity: " + std::to_string(value) +
            "%. This is temporary. Save app settings to make it permanent.");
    }
    void wheel(HWND h, int delta) {
        if (!config.mouse_control || paused || blocked)
            return;
        if (wheel_target != h) {
            wheel_target = h;
            wheel_remainder = 0;
        }
        wheel_remainder += delta;
        const int steps = wheel_remainder / WHEEL_DELTA;
        wheel_remainder %= WHEEL_DELTA;
        if (steps)
            adjust(h, steps * 5);
    }
    void toggle_window() {
        if (paused || blocked) {
            say("Resume effects before toggling a window.");
            return;
        }
        auto w = eligible(GetForegroundWindow());
        if (!w)
            return;
        if (const auto r = match_rule(config.rules, w->match); r && r->exclude) {
            say("This app is set to stay opaque. Remove that setting first.");
            return;
        }
        auto a = appearance(*w);
        if (a)
            overrides[w->identity.hwnd] = {w->identity, std::nullopt, a};
        else if (auto found = overrides.find(w->identity.hwnd);
                 found != overrides.end() && found->second.resume)
            overrides[w->identity.hwnd] = {w->identity, found->second.resume, {}};
        else {
            overrides.erase(w->identity.hwnd);
            if (!resolve_appearance(config, w->match))
                overrides[w->identity.hwnd] = {w->identity, config.defaults, {}};
        }
        known[w->identity.hwnd] = *w;
        apply_window(w->identity.hwnd);
        say(a ? "Transparency off for this window." : "Transparency on for this window.");
    }
    void toggle_pause() {
        if (paused && !recovery.alive()) {
            say("Recovery is unavailable. Quit and restart Glassify before resuming.");
            return;
        }
        paused = !paused;
        if (paused)
            restore_all();
        else
            reapply();
        mouse.set_enabled(config.mouse_control && !paused && !blocked);
        SetWindowTextW(control(pause_button), paused ? L"Resume" : L"Pause");
        say(paused ? "Paused. Window opacity, tints, and focus minimization have been restored."
                   : "Running. Saved settings and automatic defaults are active.");
    }
    void register_keys() {
        for (int id = 1; id <= 10; ++id)
            UnregisterHotKey(hwnd, id);
        if (test_instance)
            return;
        bool ok = RegisterHotKey(hwnd, 1, config.hotkey_modifiers | MOD_NOREPEAT, config.hotkey_key) != FALSE;
        const int keys[]{VK_DOWN, VK_UP, 'T', VK_LEFT, VK_RIGHT, 'H', 'M', 'F', 'G'};
        for (int i = 0; i < 9; ++i)
            ok = RegisterHotKey(hwnd, i + 2, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, keys[i]) && ok;
        if (!ok)
            say("Some shortcuts are already in use. Other controls still work.");
    }
    void hotkey(unsigned id) {
        try {
            switch (id) {
            case 1:
                toggle_pause();
                break;
            case 2:
                adjust(GetForegroundWindow(), -5);
                break;
            case 3:
                adjust(GetForegroundWindow(), 5);
                break;
            case 4:
                toggle_window();
                break;
            case 5:
                cycle_preset(-1);
                break;
            case 6:
                cycle_preset(1);
                break;
            case 7:
                ShowWindow(hwnd, IsWindowVisible(hwnd) ? SW_HIDE : SW_RESTORE);
                if (IsWindowVisible(hwnd))
                    SetForegroundWindow(hwnd);
                break;
            case 8:
                focus_suspend_target = focus.target() ? focus.target() : GetForegroundWindow();
                focus.stop();
                focus_suspended = true;
                say("Focus windows restored. Focus mode resumes when you select another app.");
                break;
            case 9:
                checked(focus_check, !config.focus_maximized);
                command(focus_check, 0);
                say(config.focus_maximized
                        ? "Focus mode is on. Other windows may minimize when the focused app fills the "
                          "screen. Ctrl+Alt+F turns it off."
                        : "Focus mode is off. Windows minimized by Glassify are restored.");
                break;
            case 10:
                web_message(std::string("{\"action\":\"toggle\",\"key\":\"globalTransparency\",\"value\":") +
                            (config.global_transparency ? "false}" : "true}"));
                break;
            }
        } catch (const std::exception &e) {
            failure(e);
        }
    }
    bool record_key(WPARAM key) {
        if (!recording)
            return false;
        if (key == VK_ESCAPE) {
            recording = false;
            say("Shortcut change canceled.");
            return true;
        }
        if (key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT || key == VK_LWIN || key == VK_RWIN)
            return true;
        unsigned mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000)
            mods |= MOD_CONTROL;
        if (GetKeyState(VK_MENU) & 0x8000)
            mods |= MOD_ALT;
        if (GetKeyState(VK_SHIFT) & 0x8000)
            mods |= MOD_SHIFT;
        if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
            mods |= MOD_WIN;
        if (!(mods & (MOD_ALT | MOD_CONTROL))) {
            say("Include Ctrl or Alt with your shortcut.");
            return true;
        }
        UnregisterHotKey(hwnd, 1);
        if (!RegisterHotKey(hwnd, 1, mods | MOD_NOREPEAT, static_cast<UINT>(key))) {
            register_keys();
            say("That shortcut is unavailable. Try another.");
            return true;
        }
        try {
            change([&] {
                config.hotkey_modifiers = mods;
                config.hotkey_key = static_cast<unsigned>(key);
            });
            recording = false;
            say("Pause shortcut saved.");
        } catch (const std::exception &e) {
            register_keys();
            failure(e);
        }
        return true;
    }
    std::optional<std::filesystem::path> file_dialog(bool saving) {
        wchar_t path[32768]{};
        OPENFILENAMEW d{};
        d.lStructSize = sizeof(d);
        d.hwndOwner = hwnd;
        d.lpstrFilter = L"JSON settings\0*.json\0\0";
        d.lpstrFile = path;
        d.nMaxFile = 32768;
        d.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        d.lpstrDefExt = L"json";
        if (saving ? GetSaveFileNameW(&d) : GetOpenFileNameW(&d))
            return path;
        return {};
    }
    void command(int id, int notification) {
        try {
            if (id >= nav_first && id < nav_first + 5) {
                switch_page(id - nav_first);
                return;
            }
            if (id >= preset_first && id < preset_first + static_cast<int>(presets().size())) {
                apply_preset(id - preset_first);
                return;
            }
            if (id >= tint_first && id < tint_first + 8) {
                selected_tint = id - tint_first;
                InvalidateRect(hwnd, nullptr, TRUE);
                if (selected)
                    SetTimer(hwnd, 6, 80, nullptr);
                return;
            }
            if (id >= default_tint_first && id < default_tint_first + 8) {
                default_tint = id - default_tint_first;
                InvalidateRect(hwnd, nullptr, TRUE);
                return;
            }
            if ((id == active_edit || id == inactive_edit || id == tint_strength_edit) &&
                notification == EN_CHANGE) {
                sync_sliders();
                return;
            }
            if (id == windows_list && notification == LBN_SELCHANGE) {
                auto i = SendMessageW(control(windows_list), LB_GETCURSEL, 0, 0);
                if (i >= 0 && static_cast<size_t>(i) < listed.size())
                    fill(listed[static_cast<size_t>(i)]);
                return;
            }
            if (id == rules_list && notification == LBN_SELCHANGE) {
                edit_rule();
                return;
            }
            switch (id) {
            case taskbar_save:
            case taskbar_reset: {
                const int opacity = id == taskbar_reset ? config.taskbar_opacity
                                                        : number(taskbar_edit, 0, 100, "Taskbar opacity");
                change([&] {
                    config.taskbar_enabled = id != taskbar_reset && checked(taskbar_check);
                    config.taskbar_opacity = opacity;
                });
                update_taskbar();
                say(config.taskbar_enabled ? "Taskbar settings saved. Check the Taskbar page for the result."
                                           : "Taskbar restored. Transparency is off.");
                break;
            }
            case 707:
                change([&] { config.ui_theme = config.ui_theme == "light" ? "dark" : "light"; });
                apply_ui_theme();
                break;
            case 708:
                web_message("{\"action\":\"setupTaskbar\"}");
                break;
            case 709:
                web.close();
                web_active = web_loading = web_failed = false;
                show_web();
                break;
            case 710:
                web_message(std::string("{\"action\":\"toggle\",\"key\":\"globalTransparency\",\"value\":") +
                            (checked(710) ? "true}" : "false}"));
                break;
            case refresh:
                refresh_windows();
                say("Window list refreshed.");
                break;
            case picker:
                say("Switch to the window you want within 3 seconds.");
                SetTimer(hwnd, 2, 3000, nullptr);
                break;
            case preview_button:
                preview();
                break;
            case keep_button:
                keep();
                break;
            case keep_window_button:
                keep_window();
                break;
            case cancel_button:
                cancel_preview();
                if (selected && same_identity(selected->identity))
                    fill(*selected);
                say("Preview undone. Saved settings are restored.");
                break;
            case delete_button: {
                auto i = SendMessageW(control(rules_list), LB_GETCURSEL, 0, 0);
                if (i >= 0 && static_cast<size_t>(i) < config.rules.size()) {
                    change([&] { config.rules.erase(config.rules.begin() + i); });
                    editing_id.clear();
                    overrides.clear();
                    cancel_preview();
                    refresh_rules();
                    reapply();
                    say("Saved app settings removed. Automatic defaults may now apply.");
                }
                break;
            }
            case auto_quick:
            case auto_apply_check: {
                const bool value = checked(id);
                change([&] { config.auto_apply = value; });
                reapply();
                say(value ? "Auto apply is on for eligible apps without saved settings."
                          : "Auto apply is off. Only saved apps and temporary overrides are affected.");
                break;
            }
            case mouse_check:
            case focus_check:
            case minimize_check:
            case close_check:
                if (id == focus_check) {
                    focus.stop();
                    focus_suspended = false;
                }
                change([&] {
                    config.mouse_control = checked(mouse_check);
                    config.focus_maximized = checked(focus_check);
                    config.minimize_to_tray = checked(minimize_check);
                    config.close_to_tray = checked(close_check);
                });
                focus_suspended = false;
                reapply();
                break;
            case save_automation: {
                Appearance a{Mode::opacity, number(default_active, 30, 100, "Default focused opacity"),
                             number(default_inactive, 30, 100, "Default background opacity"), default_tint,
                             number(default_tint_strength, 0, 40, "Default tint strength")};
                validate(a);
                change([&] {
                    config.defaults = a;
                    config.preset_id = "custom";
                });
                reapply();
                say("Automatic defaults saved. Individual app settings take priority.");
                break;
            }
            case previous_preset:
                cycle_preset(-1);
                break;
            case next_preset:
                cycle_preset(1);
                break;
            case pause_button:
                toggle_pause();
                break;
            case hide_button:
                ShowWindow(hwnd, SW_HIDE);
                break;
            case restore_button:
                paused = true;
                overrides.clear();
                restore_all();
                mouse.set_enabled(false);
                SetWindowTextW(control(pause_button), L"Resume");
                say("All window effects restored. Resume when you want to use saved settings again.");
                break;
            case restore_focus_button:
                focus_suspend_target = focus.target() ? focus.target() : GetForegroundWindow();
                focus.stop();
                focus_suspended = true;
                say("Windows minimized by focus mode restored.");
                break;
            case power_check:
                change([&] { config.power_saving = checked(power_check); });
                blocked = policy_blocks(config.power_saving);
                reapply();
                break;
            case hotkey_save:
                recording = true;
                say("Press a shortcut including Ctrl or Alt. Press Escape to cancel.");
                break;
            case startup_check: {
                HKEY key{};
                if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                                    0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
                    throw std::runtime_error("Could not change startup settings.");
                const auto value = L"\"" + executable_path().wstring() + L"\" --background";
                auto result = checked(startup_check)
                                  ? RegSetValueExW(key, L"Glassify", 0, REG_SZ,
                                                   reinterpret_cast<const BYTE *>(value.c_str()),
                                                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)))
                                  : RegDeleteValueW(key, L"Glassify");
                RegCloseKey(key);
                if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
                    throw std::runtime_error("Startup settings could not be saved.");
                say("Startup preference saved.");
                break;
            }
            case import_button:
                if (auto path = file_dialog(false)) {
                    auto replacement = parse_config(read_file(*path));
                    write_atomic(data_directory() / "config.json", serialize_config(replacement));
                    restore_all();
                    config = std::move(replacement);
                    read_only = false;
                    overrides.clear();
                    sync_settings();
                    register_keys();
                    reapply();
                    say("Settings imported.");
                }
                break;
            case export_button:
                if (auto path = file_dialog(true)) {
                    write_atomic(*path, serialize_config(config));
                    say("Settings exported.");
                }
                break;
            case data_button:
                ShellExecuteW(hwnd, L"open", data_directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case terminal_reset:
                say(reset_terminal());
                break;
            case quit_button:
                DestroyWindow(hwnd);
                break;
            }
        } catch (const std::exception &e) {
            failure(e);
        }
    }
    void slider_changed(HWND h) {
        const int id = GetDlgCtrlID(h);
        if (id == taskbar_slider) {
            SetWindowTextW(control(taskbar_edit), std::to_wstring(SendMessageW(h, TBM_GETPOS, 0, 0)).c_str());
            return;
        }
        int edit_id = id == active_slider          ? active_edit
                      : id == inactive_slider      ? inactive_edit
                      : id == tint_strength_slider ? tint_strength_edit
                                                   : 0;
        if (!edit_id)
            return;
        const auto value = SendMessageW(h, TBM_GETPOS, 0, 0);
        SetWindowTextW(control(edit_id), std::to_wstring(value).c_str());
        SetTimer(hwnd, 6, 80, nullptr);
    }
    void event(DWORD event, HWND h) {
        if (event == EVENT_OBJECT_DESTROY) {
            const bool tracked = known.contains(h);
            if (preview_target == h) {
                preview_target = nullptr;
                KillTimer(hwnd, 1);
            }
            overlays.remove(h);
            recovery.forget(h);
            known.erase(h);
            overrides.erase(h);
            if (focus.target() == h)
                focus.stop();
            if (tracked)
                schedule_list();
            return;
        }
        if (event == EVENT_OBJECT_HIDE) {
            overlays.remove(h);
            recovery.restore(h);
            if (known.contains(h))
                schedule_list();
            return;
        }
        if (event == EVENT_OBJECT_SHOW) {
            if (!test_instance && config.taskbar_enabled && inspect_taskbar(h)) {
                update_taskbar();
                return;
            }
            if (auto w = eligible(h)) {
                known[h] = *w;
                apply_window(h);
                schedule_list();
            }
            update_focus();
            return;
        }
        if (event == EVENT_OBJECT_LOCATIONCHANGE) {
            if (known.contains(h) || h == focus.target()) {
                dirty.insert(h);
                if (!geometry_pending) {
                    geometry_pending = true;
                    SetTimer(hwnd, 7, 40, nullptr);
                }
            }
            return;
        }
        if (event == EVENT_SYSTEM_FOREGROUND) {
            if (h != focus_suspend_target && eligible(h))
                focus_suspended = false;
            if (auto w = eligible(h))
                known[h] = *w;
            reapply();
            return;
        }
        if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) {
            if (IsIconic(h))
                overlays.remove(h);
            else if (auto w = eligible(h)) {
                known[h] = *w;
                apply_window(h);
            }
            update_focus();
        }
    }
    void timer(UINT_PTR id) {
        if (id == 16) {
            KillTimer(hwnd, 16);
            show_web();
            return;
        }
        if (id == 15) {
            KillTimer(hwnd, 15);
            web.close();
            web_active = web_loading = false;
            web_failed = true;
            switch_page(page);
            say("The interface took too long to open. Use Reload interface to try again.");
            return;
        }
        if (id == 14) {
            KillTimer(hwnd, 14);
            web.close();
            return;
        }
        if (id == 10) {
            KillTimer(hwnd, 10);
            publish_web();
            return;
        }
        if (id == 11) {
            KillTimer(hwnd, 11);
            if (!IsWindowVisible(hwnd) && !config.keep_interface_ready) {
                KillTimer(hwnd, 15);
                web.close();
                web_active = web_loading = false;
            }
            return;
        }
        if (id == 12) {
            KillTimer(hwnd, 12);
            apply_focus();
            return;
        }
        if (id == 13 && taskbar_setup_process &&
            WaitForSingleObject(taskbar_setup_process, 0) != WAIT_TIMEOUT) {
            KillTimer(hwnd, 13);
            CloseHandle(taskbar_setup_process);
            taskbar_setup_process = nullptr;
            if (Taskbar::installed())
                say("Taskbar engine is ready. Enable background transparency to use it.");
            else {
                try {
                    say("Taskbar setup could not finish. Check your connection and see the taskbar guide. "
                        "Setup details are in the data folder.");
                } catch (...) {
                    say("Taskbar setup failed. Open the data folder for the setup result.");
                }
            }
            SetWindowTextW(control(taskbar_status), text(control(status_label)).c_str());
            publish_web();
            return;
        }
        if (id == 9) {
            KillTimer(hwnd, 9);
            list_pending = false;
            update_list();
        }
        if (id == 1) {
            cancel_preview();
            if (selected && same_identity(selected->identity))
                fill(*selected);
            say("Preview ended. Saved settings restored.");
        }
        if (id == 2) {
            KillTimer(hwnd, 2);
            if (auto w = eligible(GetForegroundWindow())) {
                known[w->identity.hwnd] = *w;
                fill(*w);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            } else
                say("That window cannot be changed. Try a normal app window.");
        }
        if (id == 6) {
            KillTimer(hwnd, 6);
            try {
                preview();
            } catch (const std::exception &e) {
                failure(e);
            }
        }
        if (id == 7) {
            KillTimer(hwnd, 7);
            geometry_pending = false;
            auto pending = std::move(dirty);
            dirty.clear();
            for (auto h : pending)
                overlays.reposition(h);
            update_focus();
        }
        if (id == 5) {
            taskbars.poll();
            if (config.taskbar_enabled)
                update_taskbar();
            if (!recovery.alive() && !paused) {
                paused = true;
                restore_all();
                mouse.set_enabled(false);
                SetWindowTextW(control(pause_button), L"Resume");
                say("Recovery stopped. Effects restored and paused.");
            }
            const bool next = policy_blocks(config.power_saving);
            if (next != blocked) {
                blocked = next;
                if (blocked)
                    restore_all();
                else
                    reapply();
                mouse.set_enabled(config.mouse_control && !blocked && !paused);
                say(blocked ? "Effects suspended by Windows accessibility, remote-session, or "
                              "power settings."
                            : "Windows allows effects again.");
            }
        }
    }
    void shutdown() {
        web.close();
        if (taskbar_setup_process) {
            if (WaitForSingleObject(taskbar_setup_process, 0) == WAIT_TIMEOUT)
                TerminateProcess(taskbar_setup_process, ERROR_CANCELLED);
            CloseHandle(taskbar_setup_process);
            taskbar_setup_process = nullptr;
        }
        event_target = nullptr;
        mouse.stop();
        for (auto hook : hooks)
            if (hook)
                UnhookWinEvent(hook);
        for (int i = 1; i <= 10; ++i)
            UnregisterHotKey(hwnd, i);
        paused = true;
        restore_all();
        tray(false);
        PostQuitMessage(0);
    }
};
Controller *app{};
LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!app)
        return DefWindowProcW(h, m, w, l);
    if (app->taskbar_created && m == app->taskbar_created) {
        app->tray(true);
        app->update_taskbar();
        return 0;
    }
    switch (m) {
    case WM_CREATE:
        app->hwnd = h;
        try {
            app->initialize();
        } catch (const std::exception &e) {
            MessageBoxW(h, wide(e.what()).c_str(), L"Glassify could not start", MB_OK | MB_ICONERROR);
            return -1;
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        auto dc = BeginPaint(h, &ps);
        app->paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        app->paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(
            app->theme.color(reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l)));
    case WM_DRAWITEM:
        return app->draw_item(*reinterpret_cast<DRAWITEMSTRUCT *>(l));
    case WM_COMMAND:
        app->command(LOWORD(w), HIWORD(w));
        return 0;
    case WM_NOTIFY: {
        auto n = reinterpret_cast<NMHDR *>(l);
        if (n->code == NM_CUSTOMDRAW) {
            wchar_t cls[64]{};
            GetClassNameW(n->hwndFrom, cls, 64);
            if (wcscmp(cls, TRACKBAR_CLASSW) == 0)
                return app->theme.trackbar(reinterpret_cast<NMCUSTOMDRAW *>(l));
            return CDRF_DODEFAULT;
        }
        return 0;
    }
    case WM_HSCROLL:
        app->slider_changed(reinterpret_cast<HWND>(l));
        return 0;
    case WM_TIMER:
        app->timer(w);
        return 0;
    case WM_HOTKEY:
        app->hotkey(static_cast<unsigned>(w));
        return 0;
    case event_message:
        app->event(static_cast<DWORD>(w), reinterpret_cast<HWND>(l));
        return 0;
    case wheel_message:
        app->wheel(reinterpret_cast<HWND>(l), static_cast<int>(static_cast<INT_PTR>(w)));
        return 0;
    case command_message:
        if (w == 4)
            return app->recovery.process_id();
        if (w == 3)
            DestroyWindow(h);
        else if (w == 2)
            app->command(restore_button, 0);
        else {
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        return 0;
    case WM_SIZE:
        app->web.resize();
        if (w == SIZE_MINIMIZED) {
            app->was_minimized = true;
            if (app->config.minimize_to_tray)
                ShowWindow(h, SW_HIDE);
        } else if (app->was_minimized && IsWindowVisible(h)) {
            app->was_minimized = false;
            app->web_failed = false;
            app->show_web();
        }
        return 0;
    case WM_SHOWWINDOW:
        if (w) {
            app->web_failed = false;
            app->web.visible(true);
            // WM_SHOWWINDOW is sent before the visibility state is committed.
            SetTimer(h, 16, 1, nullptr);
        } else if (app->web.started()) {
            app->web.visible(false);
            SetTimer(h, 11, 3500, nullptr);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE) {
            app->show_web();
            app->update_focus();
        }
        return 0;
    case tray_message:
        if (l == WM_LBUTTONDBLCLK) {
            ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        if (l == WM_RBUTTONUP) {
            auto menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Open controls");
            AppendMenuW(menu, MF_STRING, 2, app->paused ? L"Resume effects" : L"Pause and restore windows");
            AppendMenuW(menu, MF_STRING, 3, L"Next preset");
            AppendMenuW(menu, MF_STRING | (app->config.global_transparency ? MF_CHECKED : 0), 5,
                        L"Global transparency    Ctrl+Alt+G");
            AppendMenuW(menu, MF_STRING | (app->config.focus_maximized ? MF_CHECKED : 0), 6,
                        L"Minimize other windows    Ctrl+Alt+F");
            AppendMenuW(menu, MF_STRING, 4, L"Quit and restore");
            POINT p{};
            GetCursorPos(&p);
            SetForegroundWindow(h);
            auto choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
            DestroyMenu(menu);
            if (choice == 1) {
                ShowWindow(h, SW_RESTORE);
                SetForegroundWindow(h);
            }
            if (choice == 2)
                app->toggle_pause();
            if (choice == 3)
                app->cycle_preset(1);
            if (choice == 5)
                app->hotkey(10);
            if (choice == 6)
                app->hotkey(9);
            if (choice == 4)
                DestroyWindow(h);
        }
        return 0;
    case WM_DPICHANGED: {
        app->dpi = HIWORD(w);
        app->theme.initialize(app->dpi);
        auto r = reinterpret_cast<RECT *>(l);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        for (const auto &c : app->controls) {
            SetWindowPos(c.handle, nullptr, app->scale(c.bounds.left), app->scale(c.bounds.top),
                         app->scale(c.bounds.right), app->scale(c.bounds.bottom),
                         SWP_NOZORDER | SWP_NOACTIVATE);
            SendMessageW(c.handle, WM_SETFONT,
                         reinterpret_cast<WPARAM>(c.small ? app->theme.small : app->theme.body), TRUE);
        }
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    }
    case WM_CLOSE:
        if (app->config.close_to_tray)
            ShowWindow(h, SW_HIDE);
        else
            DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        app->paused = true;
        app->restore_all();
        return TRUE;
    case WM_DESTROY:
        app->shutdown();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace
int run_application(HINSTANCE instance, PWSTR command, int show) {
    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } apartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    HANDLE token{};
    std::wstring sid = L"unknown";
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD bytes{};
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<BYTE> b(bytes);
        if (GetTokenInformation(token, TokenUser, b.data(), bytes, &bytes)) {
            PWSTR s{};
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(b.data())->User.Sid, &s)) {
                sid = s;
                LocalFree(s);
            }
        }
        CloseHandle(token);
    }
    wchar_t test[128]{};
    GetEnvironmentVariableW(L"WINFADEX_TEST_INSTANCE", test, 128);
    std::wstring suffix = test;
    if (suffix.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
        std::wstring::npos)
        return 4;
    const auto class_name = suffix.empty() ? std::wstring(L"WinFadeX.Next") : L"WinFadeX.Next." + suffix;
    const auto mutex_name = L"Local\\WinFadeXNext." + sid + (suffix.empty() ? L"" : L"." + suffix);
    auto mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (!mutex)
        return 2;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        auto existing = FindWindowW(class_name.c_str(), nullptr);
        if (existing) {
            DWORD pid{};
            GetWindowThreadProcessId(existing, &pid);
            auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                wchar_t path[32768]{};
                DWORD count = 32768;
                const bool ok = QueryFullProcessImageNameW(process, 0, path, &count) != FALSE;
                CloseHandle(process);
                if (std::wstring(command) != L"--quit" && ok &&
                    _wcsicmp(path, executable_path().c_str()) != 0)
                    MessageBoxW(existing,
                                L"Another copy of Glassify is already running from a different folder. "
                                L"Choose Quit and restore in its tray menu, then open this copy again.",
                                L"Another copy is running", MB_OK | MB_ICONINFORMATION);
            }
        }
        if (existing)
            PostMessageW(existing, command_message,
                         std::wstring(command) == L"--quit"      ? 3
                         : std::wstring(command) == L"--restore" ? 2
                                                                 : 1,
                         0);
        CloseHandle(mutex);
        return 0;
    }
    if (std::wstring(command) == L"--quit") {
        CloseHandle(mutex);
        return 0;
    }
    Controller state;
    state.instance = instance;
    state.native_only = std::wstring(command) == L"--native";
    app = &state;
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    cls.lpszClassName = class_name.c_str();
    RegisterClassW(&cls);
    const auto dpi = GetDpiForSystem();
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    state.dpi =
        std::min({dpi, static_cast<unsigned>(std::max(72L, (work.right - work.left - 64) * 96 / 1100)),
                  static_cast<unsigned>(std::max(72L, (work.bottom - work.top - 72) * 96 / 760))});
    RECT r{0, 0, MulDiv(1100, state.dpi, 96), MulDiv(760, state.dpi, 96)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
    auto h =
        CreateWindowW(cls.lpszClassName, L"Glassify - Window opacity and focus", style, CW_USEDEFAULT,
                      CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    if (!h) {
        CloseHandle(mutex);
        return 1;
    }
    SendMessageW(h, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                     GetSystemMetrics(SM_CXSMICON),
                                                     GetSystemMetrics(SM_CYSMICON), LR_SHARED)));
    ShowWindow(h, std::wstring(command) == L"--background" ? SW_HIDE : show);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (state.recording && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            state.record_key(msg.wParam);
            continue;
        }
        if (state.web_active || !IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    app = nullptr;
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
} // namespace wfx::ui
