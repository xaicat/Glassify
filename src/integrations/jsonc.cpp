#include "integrations/jsonc.hpp"
#include <cctype>
#include <map>
#include <stdexcept>

namespace wfx {
struct Node {
    size_t begin{}, end{}, key_begin{}, comma_after = std::string::npos;
    char kind{};
    std::map<std::string, Node> children;
    std::vector<std::string> order;
};
class Scanner {
    const std::string &s;
    size_t i = 0;
    void space() {
        for (;;) {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
                ++i;
            if (s.compare(i, 2, "//") == 0) {
                while (i < s.size() && s[i] != '\n')
                    ++i;
            } else if (s.compare(i, 2, "/*") == 0) {
                const auto end = s.find("*/", i + 2);
                if (end == std::string::npos)
                    throw std::runtime_error("Unterminated comment");
                i = end + 2;
            } else
                break;
        }
    }
    void string_token() {
        ++i;
        while (i < s.size()) {
            if (s[i++] == '"')
                return;
            if (s[i - 1] == '\\')
                ++i;
        }
        throw std::runtime_error("Unterminated string");
    }

  public:
    explicit Scanner(const std::string &text) : s(text) {
        if (s.compare(0, 3, "\xEF\xBB\xBF") == 0)
            i = 3;
    }
    Node node(unsigned depth = 0) {
        if (depth > 64)
            throw std::runtime_error("JSON nesting limit");
        space();
        Node n;
        n.begin = i;
        n.kind = s.at(i);
        if (n.kind == '{' || n.kind == '[') {
            const auto close = n.kind == '{' ? '}' : ']';
            ++i;
            space();
            while (s.at(i) != close) {
                const auto key_begin = i;
                std::string key;
                if (n.kind == '{') {
                    const auto start = i;
                    string_token();
                    key = parse_json(s.substr(start, i - start)).asString();
                    space();
                    if (s.at(i++) != ':')
                        throw std::runtime_error("Expected colon");
                } else
                    key = std::to_string(n.order.size());
                Node child = node(depth + 1);
                child.key_begin = key_begin;
                space();
                if (s.at(i) == ',') {
                    child.comma_after = i++;
                    space();
                } else if (s.at(i) != close)
                    throw std::runtime_error("Expected comma");
                n.order.push_back(key);
                n.children.emplace(key, std::move(child));
            }
            ++i;
        } else if (n.kind == '"')
            string_token();
        else
            while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
                   !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '/')
                ++i;
        n.end = i;
        return n;
    }
};
std::optional<Json::Value> json_at(const Json::Value &root, const JsonPath &path) {
    const Json::Value *current = &root;
    for (const auto &p : path) {
        if (current->isObject() && current->isMember(p))
            current = &(*current)[p];
        else if (current->isArray()) {
            if (p.empty() || p.find_first_not_of("0123456789") != std::string::npos)
                return {};
            const auto index = std::stoul(p);
            if (index >= current->size())
                return {};
            current = &(*current)[static_cast<Json::ArrayIndex>(index)];
        } else
            return {};
    }
    return *current;
}
std::string edit_jsonc(const std::string &source, const JsonPath &path,
                       const std::optional<Json::Value> &value) {
    parse_json(source, true);
    if (path.empty())
        throw std::runtime_error("Cannot replace settings root");
    auto tree = Scanner(source).node();
    Node *parent = &tree;
    for (size_t p = 0; p + 1 < path.size(); ++p) {
        const auto found = parent->children.find(path[p]);
        if (found == parent->children.end()) {
            if (!value)
                return source;
            Json::Value nested = *value;
            for (size_t j = path.size() - 1; j > p; --j) {
                Json::Value wrapper(Json::objectValue);
                wrapper[path[j]] = nested;
                nested = std::move(wrapper);
            }
            return edit_jsonc(source, JsonPath(path.begin(), path.begin() + static_cast<ptrdiff_t>(p + 1)),
                              nested);
        }
        parent = &found->second;
    }
    if (parent->kind != '{')
        throw std::runtime_error("Target settings parent is not an object");
    std::string out = source;
    const auto existing = parent->children.find(path.back());
    if (existing != parent->children.end()) {
        const auto &n = existing->second;
        if (value)
            out.replace(n.begin, n.end - n.begin, stringify(*value).substr(0, stringify(*value).size() - 1));
        else {
            auto start = n.key_begin, end = n.end;
            if (n.comma_after != std::string::npos)
                end = n.comma_after + 1;
            else {
                for (size_t j = 1; j < parent->order.size(); ++j)
                    if (parent->order[j] == path.back())
                        start = parent->children.at(parent->order[j - 1]).comma_after;
            }
            if (start == std::string::npos)
                throw std::runtime_error("Cannot safely remove field");
            out.erase(start, end - start);
        }
    } else if (value) {
        Json::Value key(path.back());
        auto key_text = stringify(key);
        key_text.pop_back();
        auto val_text = stringify(*value);
        val_text.pop_back();
        const std::string newline = source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        std::string insertion = newline + "  " + key_text + ": " + val_text;
        if (!parent->children.empty())
            insertion += ',';
        insertion += newline;
        out.insert(parent->begin + 1, insertion);
    }
    const auto parsed = parse_json(out, true);
    const auto actual = json_at(parsed, path);
    if ((value && (!actual || *actual != *value)) || (!value && actual))
        throw std::runtime_error("JSONC edit validation failed");
    return out;
}
} // namespace wfx
