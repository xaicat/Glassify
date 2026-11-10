#pragma once

// Shared with native UI automation. quit_button stays 142 for the measurement script.
namespace wfx::ui {
constexpr int active_slider = 202, inactive_slider = 203;
constexpr int auto_apply_check = 240, mouse_check = 241, focus_check = 242, minimize_check = 243,
              close_check = 244;
constexpr int restore_focus_button = 245, default_active = 246, default_inactive = 247,
              tint_strength_edit = 248;
constexpr int tint_strength_slider = 249, default_tint_strength = 250, save_automation = 251,
              auto_quick = 252;
constexpr int hide_button = 255, previous_preset = 256, next_preset = 257, selected_name = 266,
              keep_window_button = 269;
constexpr int nav_first = 300, preset_first = 400, tint_first = 500, default_tint_first = 600;
constexpr int removed_mode_selector = 112; // Regression assertion: this control must not exist.
constexpr int taskbar_check = 700, taskbar_slider = 701, taskbar_edit = 702, taskbar_save = 703,
              taskbar_reset = 704, taskbar_status = 705;
enum Id {
    windows_list = 101,
    refresh = 102,
    picker = 103,
    rules_list = 104,
    path_edit = 105,
    enabled_check = 110,
    exclude_check = 111,
    active_edit = 113,
    inactive_edit = 114,
    preview_button = 115,
    keep_button = 116,
    cancel_button = 117,
    delete_button = 118,
    status_label = 119,
    terminal_reset = 128,
    pause_button = 130,
    restore_button = 131,
    import_button = 133,
    export_button = 134,
    power_check = 136,
    startup_check = 137,
    hotkey_key = 139,
    hotkey_save = 140,
    data_button = 141,
    quit_button = 142
};
} // namespace wfx::ui
