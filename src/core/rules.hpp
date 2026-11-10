#pragma once
#include <string>
#include <vector>

namespace wfx {
// Legacy material values are retained for reading old settings, not as product options.
enum class Mode { unchanged, mica, mica_alt, acrylic, opacity };
struct Appearance {
    Mode mode = Mode::opacity;
    int active = 100;
    int inactive = 95;
    int tint = 0;
    int tint_strength = 12;
    bool operator==(const Appearance &) const = default;
};
struct Rule {
    std::string id;
    std::string executable;
    bool filename_only = false;
    std::string window_class;
    std::string title_pattern;
    bool enabled = true;
    bool exclude = false;
    Appearance appearance;
};
struct WindowMatch {
    std::string executable;
    std::string window_class;
    std::string title;
};
bool wildcard(const std::string &pattern, const std::string &text);
const Rule *match_rule(const std::vector<Rule> &rules, const WindowMatch &window);
std::string mode_name(Mode mode);
Mode parse_mode(const std::string &value);
void validate(const Rule &rule);
void validate(const Appearance &appearance);
struct Preset {
    std::string id, name, description;
    Appearance appearance;
};
const std::vector<Preset> &presets();
} // namespace wfx
