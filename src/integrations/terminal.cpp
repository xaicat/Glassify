#include "integrations/terminal.hpp"
#include <cstdlib>
#include <stdexcept>

namespace wfx {
static std::filesystem::path journal_path() {
    return data_directory() / "terminal-transaction.json";
}
bool terminal_integration_active() {
    return std::filesystem::exists(journal_path());
}
std::vector<std::filesystem::path> discover_terminal() {
    std::vector<std::filesystem::path> result;
    const auto local = data_directory().parent_path();
    for (const auto &relative :
         {L"Packages\\Microsoft.WindowsTerminal_8wekyb3d8bbwe\\LocalState\\settings.json",
          L"Packages\\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\\LocalState\\settings.json",
          L"Microsoft\\Windows Terminal\\settings.json"}) {
        const auto path = local / relative;
        if (std::filesystem::is_regular_file(path))
            result.push_back(path);
    }
    return result;
}
TerminalPreview preview_terminal(const std::filesystem::path &path, const std::string &guid, int opacity,
                                 std::optional<int> inactive) {
    if (opacity < 70 || opacity > 100)
        throw std::runtime_error("Terminal opacity must be 70–100");
    TerminalPreview preview;
    preview.path = std::filesystem::absolute(path);
    preview.original = read_file(path);
    preview.edited = preview.original;
    const auto root = parse_json(preview.original, true);
    if (!root.isObject() || !root["profiles"].isObject())
        throw std::runtime_error("Terminal profiles object not found");
    JsonPath prefix{"profiles", "defaults"};
    if (!guid.empty()) {
        if (!root["profiles"]["list"].isArray())
            throw std::runtime_error("Terminal profile list not found");
        bool found = false;
        for (Json::ArrayIndex i = 0; i < root["profiles"]["list"].size(); ++i)
            if (root["profiles"]["list"][i]["guid"].isString() &&
                root["profiles"]["list"][i]["guid"].asString() == guid) {
                if (found)
                    throw std::runtime_error("Ambiguous profile GUID");
                prefix = {"profiles", "list", std::to_string(i)};
                found = true;
            }
        if (!found)
            throw std::runtime_error("Profile GUID not found");
    }
    preview.journal["schemaVersion"] = 1;
    preview.journal["path"] = utf8(preview.path.wstring());
    preview.journal["profileGuid"] = guid;
    preview.journal["fields"] = Json::Value(Json::arrayValue);
    preview.journal["createdContainers"] = Json::Value(Json::arrayValue);
    const auto parent = json_at(root, prefix);
    if (parent && !parent->isObject())
        throw std::runtime_error("Profile defaults is not an object");
    if (!parent) {
        Json::Value container(Json::arrayValue);
        for (const auto &p : prefix)
            container.append(p);
        preview.journal["createdContainers"].append(container);
    }
    std::vector<std::pair<JsonPath, Json::Value>> edits{{{"useAcrylic"}, true}, {{"opacity"}, opacity}};
    if (inactive) {
        if (*inactive < 70 || *inactive > 100)
            throw std::runtime_error("Inactive opacity must be 70–100");
        edits.push_back({{"unfocusedAppearance", "useAcrylic"}, true});
        edits.push_back({{"unfocusedAppearance", "opacity"}, *inactive});
        auto container_path = prefix;
        container_path.push_back("unfocusedAppearance");
        if (!json_at(root, container_path)) {
            Json::Value container(Json::arrayValue);
            for (const auto &p : container_path)
                container.append(p);
            preview.journal["createdContainers"].append(container);
        }
    }
    for (const auto &[suffix, value] : edits) {
        auto field_path = prefix;
        field_path.insert(field_path.end(), suffix.begin(), suffix.end());
        const auto old = json_at(root, field_path);
        Json::Value field;
        field["path"] = Json::Value(Json::arrayValue);
        for (const auto &p : field_path)
            field["path"].append(p);
        field["existed"] = old.has_value();
        if (old)
            field["original"] = *old;
        field["applied"] = value;
        preview.journal["fields"].append(field);
        preview.edited = edit_jsonc(preview.edited, field_path, value);
        std::string name;
        for (const auto &p : suffix) {
            if (!name.empty())
                name += '.';
            name += p;
        }
        preview.summary += name + ": " + (old ? stringify(*old) : "<absent>\n") + "  -> " + stringify(value);
    }
    preview.summary += "\nExisting per-profile overrides and theme/Mica settings can change the result. "
                       "Windows may disable Acrylic under power/accessibility policy.\n";
    return preview;
}
void apply_terminal(const TerminalPreview &p) {
    if (terminal_integration_active())
        throw std::runtime_error("Reset the existing Terminal integration before installing another");
    if (read_file(p.path) != p.original)
        throw std::runtime_error("Terminal settings changed after preview; preview again");
    const auto backup =
        data_directory() / (L"terminal-backup-" + std::to_wstring(GetTickCount64()) + L".jsonc");
    write_atomic(backup, p.original);
    auto journal = p.journal;
    journal["backup"] = utf8(backup.wstring());
    write_atomic(journal_path(), stringify(journal));
    // Recheck immediately before the target replacement. No broad file rewrite.
    if (read_file(p.path) != p.original)
        throw std::runtime_error(
            "Concurrent settings edit; target left unchanged. Reset pending transaction before retry");
    write_atomic(p.path, p.edited);
}
std::string reset_terminal() {
    if (!terminal_integration_active())
        return "No Terminal integration is active";
    const auto journal = parse_json(read_file(journal_path()));
    if (journal["schemaVersion"] != 1 || !journal["path"].isString() || !journal["fields"].isArray())
        throw std::runtime_error("Invalid Terminal journal; target left unchanged");
    const auto path = std::filesystem::path(wide(journal["path"].asString()));
    const auto source = read_file(path);
    auto edited = source;
    const auto root = parse_json(source, true);
    unsigned restored = 0, skipped = 0;
    for (const auto &field : journal["fields"]) {
        JsonPath keys;
        for (const auto &p : field["path"])
            keys.push_back(p.asString());
        // Resolve a named profile by GUID again: the user may have reordered the list.
        const auto guid = journal.get("profileGuid", "").asString();
        if (!guid.empty()) {
            bool found = false;
            for (Json::ArrayIndex i = 0; i < root["profiles"]["list"].size(); ++i)
                if (root["profiles"]["list"][i].get("guid", "").asString() == guid) {
                    if (found)
                        throw std::runtime_error("Ambiguous GUID on reset");
                    if (keys.size() < 4)
                        throw std::runtime_error("Invalid journal field");
                    keys[2] = std::to_string(i);
                    found = true;
                }
            if (!found) {
                ++skipped;
                continue;
            }
        }
        const auto current = json_at(root, keys);
        if (current && *current == field["applied"]) {
            edited = edit_jsonc(edited, keys,
                                field["existed"].asBool() ? std::optional<Json::Value>(field["original"])
                                                          : std::nullopt);
            ++restored;
        } else
            ++skipped;
    }
    for (auto index = journal["createdContainers"].size(); index > 0; --index) {
        const auto &container = journal["createdContainers"][index - 1];
        JsonPath keys;
        for (const auto &p : container)
            keys.push_back(p.asString());
        const auto value = json_at(parse_json(edited, true), keys);
        if (value && value->isObject() && value->empty())
            edited = edit_jsonc(edited, keys, {});
    }
    if (read_file(path) != source)
        throw std::runtime_error("Concurrent Terminal settings edit; reset canceled");
    if (edited != source)
        write_atomic(path, edited);
    std::filesystem::remove(journal_path());
    return "Restored " + std::to_string(restored) + " fields; preserved " + std::to_string(skipped) +
           " externally changed fields. Backup retained.";
}
} // namespace wfx
