#pragma once
#include "core/config.hpp"
#include <optional>
#include <vector>

namespace wfx {
using JsonPath = std::vector<std::string>;
// Changes only the requested token span, preserving unrelated bytes.
std::string edit_jsonc(const std::string &source, const JsonPath &path,
                       const std::optional<Json::Value> &value);
std::optional<Json::Value> json_at(const Json::Value &root, const JsonPath &path);
} // namespace wfx
