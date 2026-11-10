#include "core/config.hpp"
#include "integrations/jsonc.hpp"
#include <iostream>
#include <stdexcept>

int checks = 0;
void check(bool condition, const char *name) {
    ++checks;
    if (!condition)
        throw std::runtime_error(name);
}
template <typename Fn> void rejects(Fn fn, const char *name) {
    try {
        fn();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(name);
}
int main() {
    try {
        using namespace wfx;
        Rule general{"a", "app.exe", true};
        general.appearance.mode = Mode::opacity;
        Rule exact{"b", "C:\\Apps\\app.exe"};
        exact.appearance.mode = Mode::acrylic;
        Rule excluded = general;
        excluded.id = "c";
        excluded.exclude = true;
        const WindowMatch window{"c:\\apps\\APP.exe", "Editor", "Document"};
        std::vector<Rule> rules{general, exact};
        check(match_rule(rules, window)->id == "b", "Exact path beats filename");
        rules.push_back(excluded);
        check(match_rule(rules, window)->exclude, "Exclusion wins");
        rules.back().enabled = false;
        check(match_rule(rules, window)->id == "b", "Disabled exclusion ignored");
        check(!match_rule(rules, {"other.exe", "", ""}), "Unknown apps unchanged");
        check(wildcard("doc*?", "Document"), "Wildcard matching");
        check(!wildcard("doc?", "document"), "Wildcard anchored");
        Config config;
        config.rules = rules;
        check(parse_config(serialize_config(config)).rules.size() == 3, "Configuration roundtrip");
        rejects([] { parse_config("{\"schemaVersion\":5,\"rules\":[]}"); }, "Future schema rejected");
        Config taskbar_config;
        taskbar_config.taskbar_enabled = true;
        taskbar_config.taskbar_opacity = 67;
        auto taskbar_roundtrip = parse_config(serialize_config(taskbar_config));
        check(taskbar_roundtrip.taskbar_enabled && taskbar_roundtrip.taskbar_opacity == 67,
              "Taskbar settings roundtrip");
        Config preferences;
        check(preferences.ui_theme == "light" && preferences.keep_interface_ready,
              "Light and instant reopening are defaults");
        for (const auto &variant : {"light", "vscode", "dark", "ember"}) {
            preferences.ui_theme = variant;
            check(parse_config(serialize_config(preferences)).ui_theme == variant, "Color themes persist");
        }
        check(parse_config("{\"schemaVersion\":4,\"rules\":[],\"uiStyle\":\"tiles\"}").ui_style == "minimal",
              "Removed Tiles style migrates to Minimal");
        preferences.keep_interface_ready = false;
        check(!parse_config(serialize_config(preferences)).keep_interface_ready,
              "Low-memory tray choice persists");
        check(!preferences.focus_maximized && !preferences.global_transparency,
              "Focus minimization and global transparency require opt-in");
        preferences.ui_style = "glass";
        preferences.global_transparency = true;
        preferences.ui_theme = "light";
        preferences.focus_delay_ms = 600;
        preferences.focus_fullscreen_only = true;
        preferences.focus_same_monitor = false;
        preferences.focus_restore_on_switch = false;
        preferences.focus_exclusions = {"Music.exe"};
        const auto saved_preferences = parse_config(serialize_config(preferences));
        check(saved_preferences.ui_style == "glass" && saved_preferences.global_transparency &&
                  saved_preferences.ui_theme == "light" && saved_preferences.focus_delay_ms == 600 &&
                  saved_preferences.focus_fullscreen_only && !saved_preferences.focus_same_monitor &&
                  !saved_preferences.focus_restore_on_switch &&
                  saved_preferences.focus_exclusions.size() == 1,
              "Theme and focus options persist");
        check(!parse_config("{\"schemaVersion\":3,\"rules\":[],\"taskbarEnabled\":true}").taskbar_enabled,
              "Legacy icon-fading taskbar mode is not silently migrated");
        rejects([] { parse_config("{\"schemaVersion\":4,\"rules\":[],\"uiTheme\":\"invalid\"}"); },
                "Unknown UI theme rejected");
        rejects([] { parse_config("{\"schemaVersion\":4,\"rules\":[],\"focusDelayMs\":0}"); },
                "Unstable zero-delay focus rejected");
        preferences.focus_exclusions = {"C:\\music.exe"};
        rejects([&] { serialize_config(preferences); }, "Focus exclusions only accept filenames");
        rejects([] { parse_config("{\"schemaVersion\":4,\"rules\":[],\"taskbarOpacity\":-1}"); },
                "Invisible taskbar rejected");
        rejects([] { parse_json("{\"x\":1,\"x\":2}"); }, "Duplicate keys rejected");
        rejects([] { parse_json("{\"x\":true,}"); }, "Strict settings reject trailing comma");
        config.rules.front().appearance.active = 29;
        rejects([&] { serialize_config(config); }, "Out-of-range opacity rejected");
        Config automatic;
        Config global;
        global.rules = {general};
        global.global_transparency = true;
        global.defaults.active = 77;
        check(resolve_appearance(global, window)->active == 77,
              "Global appearance overrides saved app appearance without changing its rule");
        check(global.rules.front().appearance.active == general.appearance.active,
              "Global mode preserves saved rules");
        check(resolve_appearance(global, {"new.exe", "", ""})->active == 77,
              "Global appearance includes new apps");
        global.rules.front().exclude = true;
        check(!resolve_appearance(global, window), "Global mode respects app exclusions");
        global.rules.front().exclude = false;
        global.rules.front().enabled = false;
        check(!resolve_appearance(global, window), "Global mode respects disabled apps");
        rejects([] { parse_config("{\"schemaVersion\":4,\"rules\":[],\"uiStyle\":\"unknown\"}"); },
                "Unknown interface style rejected");
        automatic.auto_apply = true;
        check(resolve_appearance(automatic, {"new.exe", "Editor", "New"})->inactive == 85,
              "Automatic default applies to unknown app");
        automatic.rules = {general};
        check(resolve_appearance(automatic, window)->inactive == general.appearance.inactive,
              "Explicit rule overrides default");
        automatic.rules.front().exclude = true;
        check(!resolve_appearance(automatic, window), "Exclusion overrides automatic default");
        automatic.rules.front().exclude = false;
        automatic.rules.front().enabled = false;
        check(!resolve_appearance(automatic, window), "Disabled app stays out of automatic defaults");
        automatic.auto_apply = false;
        check(!resolve_appearance(automatic, {"other.exe", "", ""}), "Automatic defaults require opt-in");
        auto migrated = parse_config("{\"schemaVersion\":1,\"rules\":[{\"id\":\"old\",\"executable\":\"app."
                                     "exe\",\"mode\":\"acrylic\"}]}");
        check(!migrated.rules[0].enabled && migrated.rules[0].appearance.mode == Mode::opacity,
              "Old material rule disabled on migration");
        check(presets().size() == 12, "Twelve presets available");
        for (const auto &p : presets())
            validate(p.appearance);
        const std::string source =
            "\xEF\xBB\xBF{\r\n // user comment\r\n "
            "\"profiles\":{\"defaults\":{\"font\":\"Cascadia\",},\"list\":[]},\r\n \"other\":42,\r\n}\r\n";
        const JsonPath opacity{"profiles", "defaults", "opacity"};
        auto edited = edit_jsonc(source, opacity, Json::Value(85));
        check(json_at(parse_json(edited, true), opacity)->asInt() == 85, "Insert JSONC field");
        check(edited.find("// user comment\r\n") != std::string::npos, "Comments and CRLF retained");
        check(edited.substr(0, 3) == source.substr(0, 3), "BOM retained");
        check(edited.find("\"font\":\"Cascadia\"") != std::string::npos, "Unrelated bytes retained");
        edited = edit_jsonc(edited, opacity, Json::Value(90));
        check(json_at(parse_json(edited, true), opacity)->asInt() == 90, "Replace JSONC field");
        edited = edit_jsonc(edited, opacity, {});
        check(!json_at(parse_json(edited, true), opacity), "Remove inserted field");
        check(parse_json(edited, true) == parse_json(source, true), "Semantic reset roundtrip");
        auto missing = edit_jsonc("{\"profiles\":{}}", opacity, Json::Value(90));
        check(json_at(parse_json(missing, true), opacity)->asInt() == 90, "Create missing ancestor");
        auto removed = edit_jsonc("{\"a\":1,\"b\":2}", {"b"}, {});
        check(parse_json(removed)["a"].asInt() == 1 && !parse_json(removed).isMember("b"),
              "Remove last field");
        auto array = edit_jsonc("{\"profiles\":{\"list\":[{\"guid\":\"x\"}]}}",
                                {"profiles", "list", "0", "opacity"}, Json::Value(80));
        check(parse_json(array, true)["profiles"]["list"][0]["opacity"].asInt() == 80, "Profile array edit");
        rejects([] { edit_jsonc("{\"profiles\":false}", {"profiles", "opacity"}, Json::Value(80)); },
                "Wrong parent rejected");
        std::cout << checks << " checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
