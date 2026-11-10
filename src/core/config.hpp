#pragma once
#include "core/rules.hpp"
#include <json/json.h>
#include <optional>
#include <string>

namespace wfx {
struct Config {
    std::vector<Rule> rules;
    bool power_saving = true;
    unsigned hotkey_modifiers = 3; // MOD_ALT | MOD_CONTROL
    unsigned hotkey_key = 'P';
    bool auto_apply = false;
    bool global_transparency = false;
    bool mouse_control = true;
    bool focus_maximized = false;
    bool focus_fullscreen_only = false;
    bool focus_same_monitor = true;
    bool focus_restore_on_switch = true;
    int focus_delay_ms = 350;
    std::vector<std::string> focus_exclusions;
    std::string ui_theme = "light";
    std::string ui_style = "minimal";
    bool keep_interface_ready = true;
    bool minimize_to_tray = true;
    bool close_to_tray = true;
    bool taskbar_enabled = false;
    int taskbar_opacity = 20;
    Appearance defaults{Mode::opacity, 95, 85};
    std::string preset_id = "custom";
};
Json::Value parse_json(const std::string &text, bool jsonc = false);
std::string stringify(const Json::Value &value);
Config parse_config(const std::string &text);
std::string serialize_config(const Config &config);
std::optional<Appearance> resolve_appearance(const Config &config, const WindowMatch &window);
} // namespace wfx
