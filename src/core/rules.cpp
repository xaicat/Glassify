#include "core/rules.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace wfx {
static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
static bool equal_name(const std::string &a, const std::string &b) {
#ifdef _WIN32
    auto convert = [](const std::string &s) {
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                             static_cast<int>(s.size()), nullptr, 0);
        std::wstring value(static_cast<size_t>(size), 0);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), value.data(),
                            size);
        return value;
    };
    const auto left = convert(a), right = convert(b);
    return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
#else
    return lower(a) == lower(b);
#endif
}
bool wildcard(const std::string &pattern, const std::string &text) {
    const auto p = lower(pattern), t = lower(text);
    size_t i = 0, j = 0, star = std::string::npos, retry = 0;
    while (j < t.size()) {
        if (i < p.size() && (p[i] == '?' || p[i] == t[j])) {
            ++i;
            ++j;
        } else if (i < p.size() && p[i] == '*') {
            star = i++;
            retry = j;
        } else if (star != std::string::npos) {
            i = star + 1;
            j = ++retry;
        } else
            return false;
    }
    while (i < p.size() && p[i] == '*')
        ++i;
    return i == p.size();
}
const Rule *match_rule(const std::vector<Rule> &rules, const WindowMatch &w) {
    const Rule *best = nullptr;
    int best_score = -1;
    for (const auto &r : rules) {
        if (!r.enabled)
            continue;
        auto executable = w.executable;
        if (r.filename_only)
            executable = executable.substr(executable.find_last_of("/\\") + 1);
        if (!equal_name(r.executable, executable))
            continue;
        if (!r.window_class.empty() && !equal_name(r.window_class, w.window_class))
            continue;
        if (!r.title_pattern.empty() && !wildcard(r.title_pattern, w.title))
            continue;
        const int score =
            (r.exclude ? 100 : 0) + (r.filename_only ? 0 : 4) + (!r.window_class.empty() ? 2 : 0);
        if (score > best_score) {
            best = &r;
            best_score = score;
        }
    }
    return best;
}
std::string mode_name(Mode m) {
    switch (m) {
    case Mode::mica:
        return "mica";
    case Mode::mica_alt:
        return "mica-alt";
    case Mode::acrylic:
        return "acrylic";
    case Mode::opacity:
        return "opacity";
    default:
        return "unchanged";
    }
}
Mode parse_mode(const std::string &s) {
    for (const auto mode : {Mode::unchanged, Mode::mica, Mode::mica_alt, Mode::acrylic, Mode::opacity})
        if (mode_name(mode) == s)
            return mode;
    throw std::runtime_error("Unknown appearance mode");
}
void validate(const Rule &r) {
    if (r.id.empty() || r.executable.empty() || r.id.size() > 128 || r.executable.size() > 32768)
        throw std::runtime_error("Rule needs an ID and executable");
    if (r.filename_only && r.executable.find_first_of("/\\") != std::string::npos)
        throw std::runtime_error("Filename rule must not contain a path");
    validate(r.appearance);
}
void validate(const Appearance &a) {
    if (a.active < 30 || a.active > 100 || a.inactive < 30 || a.inactive > 100)
        throw std::runtime_error("Opacity must be between 30 and 100 percent");
    if (a.tint < 0 || a.tint > 7 || a.tint_strength < 0 || a.tint_strength > 40)
        throw std::runtime_error("Tint must use a supported color and a strength from 0 to 40 percent");
}
const std::vector<Preset> &presets() {
    static const std::vector<Preset> values{
        {"clear", "Clear", "100% focused / 100% background", {Mode::opacity, 100, 100}},
        {"subtle", "Subtle", "95% focused / 90% background", {Mode::opacity, 95, 90}},
        {"balanced", "Balanced", "90% focused / 80% background", {Mode::opacity, 90, 80}},
        {"airy", "Airy", "85% focused / 70% background", {Mode::opacity, 85, 70}},
        {"deep", "Deep", "75% focused / 60% background", {Mode::opacity, 75, 60}},
        {"focus", "Focus", "100% focused / 65% background", {Mode::opacity, 100, 65}},
        {"ocean", "Ocean", "90% / 80% with a blue tint", {Mode::opacity, 90, 80, 1, 12}},
        {"violet", "Violet", "90% / 80% with a violet tint", {Mode::opacity, 90, 80, 2, 12}},
        {"rose", "Rose", "90% / 80% with a rose tint", {Mode::opacity, 90, 80, 3, 10}},
        {"amber", "Amber", "95% / 80% with a warm tint", {Mode::opacity, 95, 80, 4, 10}},
        {"forest", "Forest", "90% / 80% with a green tint", {Mode::opacity, 90, 80, 5, 12}},
        {"night", "Night", "90% / 75% with a dark tint", {Mode::opacity, 90, 75, 7, 18}}};
    return values;
}
} // namespace wfx
