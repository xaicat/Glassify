#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <windows.h>

namespace wfx::ui {
class WebHost {
    struct State;
    std::shared_ptr<State> state;

  public:
    using Message = std::function<void(const std::string &)>;
    ~WebHost();
    bool start(HWND parent, Message message, std::function<void(bool)> ready,
               const std::string &theme = "light", const std::string &style = "minimal");
    void visible(bool show);
    void present();
    void close();
    void resize();
    void send(const std::string &json);
    bool started() const {
        return static_cast<bool>(state);
    }
    bool ready() const;
    void evaluate(const std::wstring &script, std::function<void(const std::string &)> result);
};
} // namespace wfx::ui
