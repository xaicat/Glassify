#include "core/config.hpp"
#include <memory>
#include <set>
#include <stdexcept>

namespace wfx {
Json::Value parse_json(const std::string &text, bool jsonc) {
    if (text.size() > 4 * 1024 * 1024)
        throw std::runtime_error("Settings file exceeds 4 MB limit");
    Json::CharReaderBuilder builder;
    builder["allowComments"] = jsonc;
    builder["allowTrailingCommas"] = jsonc;
    builder["collectComments"] = false;
    builder["rejectDupKeys"] = true;
    builder["failIfExtra"] = true;
    builder["stackLimit"] = 64;
    builder["skipBom"] = true;
    Json::Value root;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors))
        throw std::runtime_error("Invalid JSON: " + errors);
    return root;
}
std::string stringify(const Json::Value &v) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, v) + "\n";
}
static std::string required_string(const Json::Value &v, const char *key) {
    if (!v[key].isString())
        throw std::runtime_error(std::string("Expected string: ") + key);
    return v[key].asString();
}
static bool boolean(const Json::Value &v, const char *key, bool fallback) {
    if (!v.isMember(key))
        return fallback;
    if (!v[key].isBool())
        throw std::runtime_error(std::string("Expected boolean: ") + key);
    return v[key].asBool();
}
static int integer(const Json::Value &v, const char *key, int fallback) {
    if (!v.isMember(key))
        return fallback;
    if (!v[key].isInt())
        throw std::runtime_error(std::string("Expected integer: ") + key);
    return v[key].asInt();
}
static Appearance read_appearance(const Json::Value &v, Appearance fallback = {}) {
    auto a = fallback;
    a.mode = Mode::opacity;
    a.active = integer(v, "activeOpacity", a.active);
    a.inactive = integer(v, "inactiveOpacity", a.inactive);
    a.tint = integer(v, "tint", a.tint);
    a.tint_strength = integer(v, "tintStrength", a.tint_strength);
    validate(a);
    return a;
}
static Json::Value appearance_json(const Appearance &a) {
    validate(a);
    Json::Value v;
    v["activeOpacity"] = a.active;
    v["inactiveOpacity"] = a.inactive;
    v["tint"] = a.tint;
    v["tintStrength"] = a.tint_strength;
    return v;
}
Config parse_config(const std::string &text) {
    const auto root = parse_json(text);
    if (!root.isObject() || !root["schemaVersion"].isInt() ||
        (root["schemaVersion"].asInt() < 1 || root["schemaVersion"].asInt() > 4))
        throw std::runtime_error("Unsupported configuration version; file left unchanged");
    if (!root["rules"].isArray() || root["rules"].size() > 512)
        throw std::runtime_error("Expected at most 512 rules");
    Config config;
    config.power_saving = boolean(root, "powerSaving", true);
    config.auto_apply = boolean(root, "autoApply", false);
    config.global_transparency = boolean(root, "globalTransparency", false);
    config.mouse_control = boolean(root, "mouseControl", true);
    config.focus_maximized = boolean(root, "focusMaximized", false);
    config.focus_fullscreen_only = boolean(root, "focusFullscreenOnly", false);
    config.focus_same_monitor = boolean(root, "focusSameMonitor", true);
    config.focus_restore_on_switch = boolean(root, "focusRestoreOnSwitch", true);
    config.focus_delay_ms = integer(root, "focusDelayMs", 350);
    if (config.focus_delay_ms < 150 || config.focus_delay_ms > 1500)
        throw std::runtime_error("Focus delay must be from 150 to 1500 milliseconds");
    if (root.isMember("uiTheme"))
        config.ui_theme = required_string(root, "uiTheme");
    if (config.ui_theme != "dark" && config.ui_theme != "light" && config.ui_theme != "vscode" &&
        config.ui_theme != "ember")
        throw std::runtime_error("Unknown interface theme");
    if (root.isMember("uiStyle"))
        config.ui_style = required_string(root, "uiStyle");
    if (config.ui_style == "tiles")
        config.ui_style = "minimal";
    if (config.ui_style != "minimal" && config.ui_style != "glass")
        throw std::runtime_error("Unknown interface style");
    config.keep_interface_ready = boolean(root, "keepInterfaceReady", true);
    if (root.isMember("focusExclusions")) {
        if (!root["focusExclusions"].isArray() || root["focusExclusions"].size() > 64)
            throw std::runtime_error("Expected at most 64 focus exclusions");
        for (const auto &item : root["focusExclusions"]) {
            if (!item.isString() || item.asString().empty() || item.asString().size() > 260 ||
                item.asString().find_first_of("/\\") != std::string::npos)
                throw std::runtime_error("Focus exclusions must be executable filenames");
            config.focus_exclusions.push_back(item.asString());
        }
    }
    config.minimize_to_tray = boolean(root, "minimizeToTray", true);
    config.close_to_tray = boolean(root, "closeToTray", true);
    config.taskbar_enabled = boolean(root, "taskbarEnabled", false) && root["schemaVersion"].asInt() >= 4;
    config.taskbar_opacity = integer(root, "taskbarOpacity", 20);
    if (config.taskbar_opacity < 0 || config.taskbar_opacity > 100)
        throw std::runtime_error("Taskbar opacity must be from 0 to 100");
    if (root.isMember("defaults")) {
        if (!root["defaults"].isObject())
            throw std::runtime_error("Defaults must be an object");
        config.defaults = read_appearance(root["defaults"], config.defaults);
    }
    if (root.isMember("presetId"))
        config.preset_id = required_string(root, "presetId");
    const int mods = integer(root, "hotkeyModifiers", 3), key = integer(root, "hotkeyKey", 'P');
    if (mods < 0 || mods > 15 || key < 1 || key > 254)
        throw std::runtime_error("Invalid hotkey");
    config.hotkey_modifiers = static_cast<unsigned>(mods);
    config.hotkey_key = static_cast<unsigned>(key);
    std::set<std::string> ids;
    for (const auto &v : root["rules"]) {
        Rule r;
        r.id = required_string(v, "id");
        r.executable = required_string(v, "executable");
        r.filename_only = boolean(v, "filenameOnly", false);
        if (v.isMember("windowClass"))
            r.window_class = required_string(v, "windowClass");
        if (v.isMember("titlePattern"))
            r.title_pattern = required_string(v, "titlePattern");
        r.enabled = boolean(v, "enabled", true);
        r.exclude = boolean(v, "exclude", false);
        r.appearance = read_appearance(v);
        if (v.isMember("mode")) {
            const auto old_mode = parse_mode(required_string(v, "mode"));
            if (old_mode != Mode::opacity)
                r.enabled = false;
        }
        validate(r);
        if (!ids.insert(r.id).second)
            throw std::runtime_error("Duplicate rule ID");
        config.rules.push_back(std::move(r));
    }
    return config;
}
std::string serialize_config(const Config &config) {
    if (config.rules.size() > 512 || config.hotkey_modifiers > 15 || config.hotkey_key < 1 ||
        config.hotkey_key > 254 || config.taskbar_opacity < 0 || config.taskbar_opacity > 100 ||
        config.focus_delay_ms < 150 || config.focus_delay_ms > 1500 || config.focus_exclusions.size() > 64 ||
        (config.ui_theme != "dark" && config.ui_theme != "light" && config.ui_theme != "vscode" &&
         config.ui_theme != "ember") ||
        (config.ui_style != "minimal" && config.ui_style != "glass"))
        throw std::runtime_error("Configuration limits exceeded");
    for (const auto &item : config.focus_exclusions)
        if (item.empty() || item.size() > 260 || item.find_first_of("/\\") != std::string::npos)
            throw std::runtime_error("Focus exclusions must be executable filenames");
    std::set<std::string> ids;
    for (const auto &rule : config.rules)
        if (!ids.insert(rule.id).second)
            throw std::runtime_error("Duplicate rule ID");
    Json::Value root(Json::objectValue);
    root["schemaVersion"] = 4;
    root["uiTheme"] = config.ui_theme;
    root["uiStyle"] = config.ui_style;
    root["keepInterfaceReady"] = config.keep_interface_ready;
    root["globalTransparency"] = config.global_transparency;
    root["focusFullscreenOnly"] = config.focus_fullscreen_only;
    root["focusSameMonitor"] = config.focus_same_monitor;
    root["focusRestoreOnSwitch"] = config.focus_restore_on_switch;
    root["focusDelayMs"] = config.focus_delay_ms;
    root["focusExclusions"] = Json::Value(Json::arrayValue);
    for (const auto &item : config.focus_exclusions)
        root["focusExclusions"].append(item);
    root["taskbarEnabled"] = config.taskbar_enabled;
    root["taskbarOpacity"] = config.taskbar_opacity;
    root["autoApply"] = config.auto_apply;
    root["mouseControl"] = config.mouse_control;
    root["focusMaximized"] = config.focus_maximized;
    root["minimizeToTray"] = config.minimize_to_tray;
    root["closeToTray"] = config.close_to_tray;
    root["defaults"] = appearance_json(config.defaults);
    root["presetId"] = config.preset_id;
    root["powerSaving"] = config.power_saving;
    root["hotkeyModifiers"] = config.hotkey_modifiers;
    root["hotkeyKey"] = config.hotkey_key;
    root["rules"] = Json::Value(Json::arrayValue);
    for (const auto &r : config.rules) {
        validate(r);
        Json::Value v = appearance_json(r.appearance);
        v["id"] = r.id;
        v["executable"] = r.executable;
        v["filenameOnly"] = r.filename_only;
        v["windowClass"] = r.window_class;
        v["titlePattern"] = r.title_pattern;
        v["enabled"] = r.enabled;
        v["exclude"] = r.exclude;
        v["mode"] = "opacity";
        v["activeOpacity"] = r.appearance.active;
        v["inactiveOpacity"] = r.appearance.inactive;
        root["rules"].append(v);
    }
    return stringify(root);
}
std::optional<Appearance> resolve_appearance(const Config &c, const WindowMatch &w) {
    if (const auto r = match_rule(c.rules, w)) {
        if (r->exclude)
            return {};
        return c.global_transparency ? c.defaults : r->appearance;
    }
    // An explicitly disabled app never falls through into the automatic default.
    for (const auto &r : c.rules)
        if (!r.enabled) {
            auto enabled = r;
            enabled.enabled = true;
            if (match_rule(std::vector<Rule>{enabled}, w))
                return {};
        }
    if (c.auto_apply || c.global_transparency)
        return c.defaults;
    return {};
}
} // namespace wfx
