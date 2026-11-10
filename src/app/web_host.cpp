#include "app/web_host.hpp"
#include "platform/windows.hpp"
#include <WebView2.h>

#include "app/webview_uuids.hpp"
#include <atomic>

namespace wfx::ui {
namespace {
template <typename Interface, typename... Args> class Callback final : public Interface {
    std::atomic<ULONG> references{1};
    std::function<HRESULT(Args...)> function;

  public:
    explicit Callback(std::function<HRESULT(Args...)> f) : function(std::move(f)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **object) override {
        if (!object)
            return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(Interface)) {
            *object = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override {
        return function(args...);
    }
};
template <typename Interface, typename... Args, typename F> Interface *callback(F f) {
    return new Callback<Interface, Args...>(std::move(f));
}
bool trusted(const wchar_t *uri) {
    return uri && std::wstring_view(uri).starts_with(L"https://glassify.local/");
}
} // namespace
struct WebHost::State {
    HWND parent{};
    bool alive = true, loaded = false, presentable = false;
    ICoreWebView2Controller *controller{};
    ICoreWebView2 *view{};
    Message message;
    std::function<void(bool)> on_ready;
    ~State() {
        if (controller) {
            controller->Close();
            controller->Release();
        }
        if (view)
            view->Release();
    }
    void complete(bool ok) {
        if (!alive)
            return;
        loaded = ok;
        if (!ok && controller)
            controller->put_IsVisible(FALSE);
        if (auto notify = on_ready)
            notify(ok);
    }
};
WebHost::~WebHost() {
    close();
}
bool WebHost::ready() const {
    return state && state->loaded;
}
void WebHost::close() {
    if (state) {
        state->alive = false;
        state->message = {};
        state->on_ready = {};
        if (state->controller)
            state->controller->Close();
        state.reset();
    }
}
bool WebHost::start(HWND parent, Message message, std::function<void(bool)> ready_callback,
                    const std::string &theme, const std::string &style) {
    if (state)
        return true;
    const auto root = executable_path().parent_path();
    if (!std::filesystem::exists(root / "web" / "index.html"))
        return false;
    // Load only the explicitly packaged loader, never a DLL from the current directory.
    static HMODULE loader = LoadLibraryExW((root / "WebView2Loader.dll").c_str(), nullptr,
                                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loader)
        return false;
    using Create = HRESULT(STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                                             ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
    const auto create =
        reinterpret_cast<Create>(GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions"));
    if (!create)
        return false;
    state = std::make_shared<State>();
    auto s = state;
    s->parent = parent;
    s->message = std::move(message);
    s->on_ready = std::move(ready_callback);
    auto environment = callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                                ICoreWebView2Environment *>(
        [s, root, theme, style](HRESULT hr, ICoreWebView2Environment *env) -> HRESULT {
            if (!s->alive)
                return S_OK;
            if (FAILED(hr) || !env) {
                s->complete(false);
                return S_OK;
            }
            auto controller = callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                       ICoreWebView2Controller *>(
                [s, root, theme, style](HRESULT result, ICoreWebView2Controller *c) -> HRESULT {
                    if (!s->alive)
                        return S_OK;
                    if (FAILED(result) || !c) {
                        s->complete(false);
                        return S_OK;
                    }
                    s->controller = c;
                    c->AddRef();
                    ICoreWebView2Controller2 *background{};
                    if (SUCCEEDED(c->QueryInterface(__uuidof(ICoreWebView2Controller2),
                                                    reinterpret_cast<void **>(&background)))) {
                        const auto color = theme == "light"    ? COREWEBVIEW2_COLOR{255, 247, 248, 250}
                                           : theme == "vscode" ? COREWEBVIEW2_COLOR{255, 31, 31, 31}
                                           : theme == "ember"  ? COREWEBVIEW2_COLOR{255, 24, 24, 26}
                                                               : COREWEBVIEW2_COLOR{255, 12, 20, 36};
                        background->put_DefaultBackgroundColor(color);
                        background->Release();
                    }
                    c->get_CoreWebView2(&s->view);
                    if (!s->view) {
                        s->complete(false);
                        return S_OK;
                    }
                    ICoreWebView2_3 *mapping{};
                    if (FAILED(s->view->QueryInterface(__uuidof(ICoreWebView2_3),
                                                       reinterpret_cast<void **>(&mapping)))) {
                        s->complete(false);
                        return S_OK;
                    }
                    result = mapping->SetVirtualHostNameToFolderMapping(
                        L"glassify.local", (root / "web").c_str(),
                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
                    mapping->Release();
                    if (FAILED(result)) {
                        s->complete(false);
                        return S_OK;
                    }
                    ICoreWebView2Settings *settings{};
                    if (SUCCEEDED(s->view->get_Settings(&settings))) {
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(FALSE);
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->Release();
                    }
                    EventRegistrationToken token{};
                    auto nav = callback<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *,
                                        ICoreWebView2NavigationStartingEventArgs *>(
                        [](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
                            LPWSTR uri{};
                            args->get_Uri(&uri);
                            args->put_Cancel(!trusted(uri));
                            CoTaskMemFree(uri);
                            return S_OK;
                        });
                    s->view->add_NavigationStarting(nav, &token);
                    nav->Release();
                    auto popup = callback<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2 *,
                                          ICoreWebView2NewWindowRequestedEventArgs *>(
                        [](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
                            args->put_Handled(TRUE);
                            return S_OK;
                        });
                    s->view->add_NewWindowRequested(popup, &token);
                    popup->Release();
                    auto permission = callback<ICoreWebView2PermissionRequestedEventHandler, ICoreWebView2 *,
                                               ICoreWebView2PermissionRequestedEventArgs *>(
                        [](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *args) -> HRESULT {
                            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                            return S_OK;
                        });
                    s->view->add_PermissionRequested(permission, &token);
                    permission->Release();
                    // Weak captures avoid COM event-handler cycles keeping browser processes alive.
                    std::weak_ptr<State> weak = s;
                    auto failed = callback<ICoreWebView2ProcessFailedEventHandler, ICoreWebView2 *,
                                           ICoreWebView2ProcessFailedEventArgs *>(
                        [weak](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
                            COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                            args->get_ProcessFailedKind(&kind);
                            if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                                kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
                                kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE)
                                if (auto state = weak.lock(); state && state->alive)
                                    state->complete(false);
                            return S_OK;
                        });
                    s->view->add_ProcessFailed(failed, &token);
                    failed->Release();
                    auto message = callback<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2 *,
                                            ICoreWebView2WebMessageReceivedEventArgs *>(
                        [weak](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                            auto s = weak.lock();
                            if (!s || !s->alive)
                                return S_OK;
                            LPWSTR source{}, json{};
                            args->get_Source(&source);
                            if (trusted(source) && SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                                if (auto receive = s->message)
                                    receive(utf8(json));
                            }
                            CoTaskMemFree(source);
                            CoTaskMemFree(json);
                            return S_OK;
                        });
                    s->view->add_WebMessageReceived(message, &token);
                    message->Release();
                    auto navigation = callback<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2 *,
                                               ICoreWebView2NavigationCompletedEventArgs *>(
                        [weak](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
                            if (auto s = weak.lock(); s && s->alive) {
                                BOOL ok{};
                                args->get_IsSuccess(&ok);
                                s->complete(ok != FALSE);
                                if (ok && GetForegroundWindow() == s->parent)
                                    s->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                            }
                            return S_OK;
                        });
                    s->view->add_NavigationCompleted(navigation, &token);
                    navigation->Release();
                    RECT bounds{};
                    GetClientRect(s->parent, &bounds);
                    c->put_Bounds(bounds);
                    // Keep the initial about:blank surface hidden until local content is ready.
                    c->put_IsVisible(FALSE);
                    s->view->Navigate(
                        wide("https://glassify.local/index.html?theme=" + theme + "&style=" + style).c_str());
                    return S_OK;
                });
            const auto result = env->CreateCoreWebView2Controller(s->parent, controller);
            controller->Release();
            if (FAILED(result))
                s->complete(false);
            return S_OK;
        });
    const auto profile = data_directory() / "WebView2";
    const auto hr = create(nullptr, profile.c_str(), nullptr, environment);
    environment->Release();
    if (FAILED(hr)) {
        close();
        return false;
    }
    return true;
}
void WebHost::resize() {
    if (state && state->controller) {
        RECT r{};
        GetClientRect(state->parent, &r);
        state->controller->put_Bounds(r);
        state->controller->put_IsVisible(state->loaded && state->presentable &&
                                         IsWindowVisible(state->parent) && !IsIconic(state->parent));
        state->controller->NotifyParentWindowPositionChanged();
    }
}
void WebHost::visible(bool show) {
    if (state && state->controller)
        state->controller->put_IsVisible(show && state->loaded && state->presentable);
}
void WebHost::present() {
    if (state)
        state->presentable = true;
    resize();
}
void WebHost::send(const std::string &json) {
    if (ready())
        state->view->PostWebMessageAsJson(wide(json).c_str());
}
void WebHost::evaluate(const std::wstring &script, std::function<void(const std::string &)> result) {
    if (!ready())
        return;
    auto handler = callback<ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
        [result](HRESULT hr, LPCWSTR json) -> HRESULT {
            result(SUCCEEDED(hr) && json ? utf8(json) : "null");
            return S_OK;
        });
    state->view->ExecuteScript(script.c_str(), handler);
    handler->Release();
}
} // namespace wfx::ui
