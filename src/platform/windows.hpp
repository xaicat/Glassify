#pragma once
#include "core/rules.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace wfx {
constexpr DWORD backdrop_attribute = 38;
constexpr wchar_t owner_property[] = L"WinFadeXNext.Transaction.v1";
struct Identity {
    HWND hwnd{};
    DWORD pid{};
    unsigned long long created{};
};
struct CompanionRecord {
    DWORD pid{};
    unsigned long long created{};
};
struct Window {
    Identity identity;
    WindowMatch match;
    std::wstring title;
};
struct EffectState {
    bool layered{};
    BYTE alpha = 255;
    COLORREF color{};
    DWORD flags{};
    int backdrop{};
};
struct Record {
    bool used{};
    Identity identity;
    ULONG_PTR cookie{};
    Mode mode{};
    EffectState original, before, after;
};
constexpr wchar_t focus_property[] = L"WinFadeXNext.Focus.v2";
struct MinimizedRecord {
    bool used{};
    Identity identity;
    ULONG_PTR cookie{};
    WINDOWPLACEMENT placement{};
};
bool restore_minimized_record(const MinimizedRecord &record);
std::wstring wide(const std::string &value);
std::string utf8(const std::wstring &value);
std::optional<Identity> identify(HWND hwnd);
bool same_identity(const Identity &id);
std::optional<Window> inspect(HWND hwnd, bool include_own = false, bool allow_fullscreen = false);
std::vector<Window> enumerate();
std::string exclusion(HWND hwnd, bool include_own = false, bool allow_fullscreen = false);
bool focus_window(HWND hwnd);
bool fullscreen_window(HWND hwnd);
std::optional<EffectState> capture(HWND hwnd, Mode mode);
EffectState desired(const EffectState &state, Mode mode, int opacity);
bool state_matches(const EffectState &a, const EffectState &b, Mode mode);
bool set_effect(HWND hwnd, Mode mode, const EffectState &state);
bool restore_record(const Record &record);
std::filesystem::path data_directory();
std::filesystem::path executable_path();
std::string read_file(const std::filesystem::path &path);
void write_atomic(const std::filesystem::path &path, const std::string &data);
void log(const std::string &message);
bool policy_blocks(bool power_saving);
unsigned windows_build();
} // namespace wfx
