#pragma once
#include "integrations/jsonc.hpp"
#include "platform/windows.hpp"

namespace wfx {
struct TerminalPreview {
    std::filesystem::path path;
    std::string original, edited;
    Json::Value journal;
    std::string summary;
};
std::vector<std::filesystem::path> discover_terminal();
TerminalPreview preview_terminal(const std::filesystem::path &path, const std::string &profile_guid,
                                 int opacity, std::optional<int> inactive = {});
void apply_terminal(const TerminalPreview &preview);
std::string reset_terminal();
bool terminal_integration_active();
} // namespace wfx
