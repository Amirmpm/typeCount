#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define WEBVIEW_EDGE 1

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>

#include "EventToken.h"
#include "WebView2.h"
#include "detail/platform/windows/webview2/loader.hh"

#include "platform.hpp"
#include "ui_bundle.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace tc {
namespace win {

static constexpr IID TC_IID_WebResourceRequestedEventHandler{
    0xAB00B74C, 0x15F1, 0x4646, {0x80, 0xE8, 0xE7, 0x63, 0x41, 0xD2, 0x5D, 0x71}};

class WebViewHost {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using ReadyCallback = std::function<void()>;

    WebViewHost() = default;
    ~WebViewHost() { destroy(); }

    WebViewHost(const WebViewHost&) = delete;
    WebViewHost& operator=(const WebViewHost&) = delete;

    bool init(HWND parent,
              const std::wstring& user_data_dir,
              const std::string& initial_locale,
              const std::string& initial_theme,
              MessageCallback on_msg,
              ReadyCallback on_ready,
              std::function<void()> on_runtime_missing) {
        destroy();
        parent_ = parent;
        user_data_dir_ = user_data_dir;
        initial_locale_ = initial_locale;
        initial_theme_ = initial_theme;
        on_msg_ = std::move(on_msg);
        on_ready_ = std::move(on_ready);
        on_runtime_missing_ = std::move(on_runtime_missing);

        handler_ = new ComHandler(this);
        handler_->AddRef();

        HRESULT hr = loader_.create_environment_with_options(
            nullptr, user_data_dir_.c_str(), nullptr, handler_);
        if (FAILED(hr)) {
            destroy();
            if (on_runtime_missing_) on_runtime_missing_();
            return false;
        }
        initializing_ = true;
        return true;
    }

    void resize_to_parent() {
        if (!controller_ || !parent_) return;
        RECT rc{};
        GetClientRect(parent_, &rc);
        controller_->put_Bounds(rc);
    }

    void set_visible(bool vis) {
        visible_ = vis;
        if (controller_) {
            controller_->put_IsVisible(vis ? TRUE : FALSE);
        }
    }

    void focus() {
        if (controller_) {
            controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
    }

    void eval_js(const std::string& script) {
        if (!webview_) {
            pending_scripts_.push_back(script);
            return;
        }
        std::wstring ws = to_wide(script);
        webview_->ExecuteScript(ws.c_str(), nullptr);
    }

    void destroy() {
        initializing_ = false;
        ready_ = false;
        pending_scripts_.clear();
        if (handler_) {
            handler_->detach();
            handler_->Release();
            handler_ = nullptr;
        }
        if (webview_) {
            webview_->Release();
            webview_ = nullptr;
        }
        if (controller_) {
            controller_->Close();
            controller_->Release();
            controller_ = nullptr;
        }
        if (env_) {
            env_->Release();
            env_ = nullptr;
        }
    }

    bool is_alive() const { return webview_ != nullptr || initializing_; }
    bool is_ready() const { return ready_; }

private:
    class ComHandler
        : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
          public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
          public ICoreWebView2WebMessageReceivedEventHandler,
          public ICoreWebView2PermissionRequestedEventHandler,
          public ICoreWebView2WebResourceRequestedEventHandler {
    public:
        explicit ComHandler(WebViewHost* owner) : owner_(owner) {}
        virtual ~ComHandler() = default;

        void detach() { owner_ = nullptr; }

        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
        ULONG STDMETHODCALLTYPE Release() override {
            ULONG r = --refs_;
            if (r == 0) delete this;
            return r;
        }
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppv) override {
            if (!ppv) return E_POINTER;
            if (IsEqualIID(riid, IID_IUnknown) ||
                IsEqualIID(riid, webview::detail::mswebview2::IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
                auto* p = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
                p->AddRef(); *ppv = p; return S_OK;
            }
            if (IsEqualIID(riid, webview::detail::mswebview2::IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
                auto* p = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
                p->AddRef(); *ppv = p; return S_OK;
            }
            if (IsEqualIID(riid, webview::detail::mswebview2::IID_ICoreWebView2WebMessageReceivedEventHandler)) {
                auto* p = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
                p->AddRef(); *ppv = p; return S_OK;
            }
            if (IsEqualIID(riid, webview::detail::mswebview2::IID_ICoreWebView2PermissionRequestedEventHandler)) {
                auto* p = static_cast<ICoreWebView2PermissionRequestedEventHandler*>(this);
                p->AddRef(); *ppv = p; return S_OK;
            }
            if (IsEqualIID(riid, TC_IID_WebResourceRequestedEventHandler)) {
                auto* p = static_cast<ICoreWebView2WebResourceRequestedEventHandler*>(this);
                p->AddRef(); *ppv = p; return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }

        HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Environment* env) override {
            if (!owner_) return S_OK;
            if (FAILED(res) || !env) {
                owner_->initializing_ = false;
                if (owner_->on_runtime_missing_) owner_->on_runtime_missing_();
                return S_OK;
            }
            owner_->env_ = env;
            env->AddRef();
            env->CreateCoreWebView2Controller(owner_->parent_, this);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Controller* controller) override {
            if (!owner_) return S_OK;
            owner_->initializing_ = false;
            if (FAILED(res) || !controller) {
                if (owner_->on_runtime_missing_) owner_->on_runtime_missing_();
                return S_OK;
            }
            owner_->controller_ = controller;
            controller->AddRef();
            controller->get_CoreWebView2(&owner_->webview_);
            if (!owner_->webview_) return S_OK;

            ICoreWebView2Settings* settings = nullptr;
            if (SUCCEEDED(owner_->webview_->get_Settings(&settings)) && settings) {
                settings->put_AreDefaultContextMenusEnabled(TRUE);
                settings->put_AreDevToolsEnabled(FALSE);
                settings->put_IsStatusBarEnabled(FALSE);
                settings->put_IsZoomControlEnabled(FALSE);
                settings->Release();
            }

            EventRegistrationToken tok{};
            owner_->webview_->add_WebMessageReceived(this, &tok);
            owner_->webview_->add_PermissionRequested(this, &tok);
            owner_->webview_->add_WebResourceRequested(this, &tok);
            owner_->webview_->AddWebResourceRequestedFilter(
                L"https://app.typecount.local/*",
                COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);

            std::string boot =
                "window.clickpulseLocale=" + JsonValue::escape_string(owner_->initial_locale_) + ";"
                "window.clickpulsePlatform='windows';"
                "window.clickpulseTheme=" + JsonValue::escape_string(owner_->initial_theme_) + ";";
            owner_->webview_->AddScriptToExecuteOnDocumentCreated(to_wide(boot).c_str(), nullptr);

            owner_->resize_to_parent();
            owner_->set_visible(owner_->visible_);
            owner_->ready_ = true;

            owner_->webview_->Navigate(L"https://app.typecount.local/index.html");

            if (!owner_->pending_scripts_.empty()) {
                for (const auto& s : owner_->pending_scripts_) {
                    owner_->webview_->ExecuteScript(to_wide(s).c_str(), nullptr);
                }
                owner_->pending_scripts_.clear();
            }
            if (owner_->on_ready_) owner_->on_ready_();
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) override {
            if (!owner_ || !args) return S_OK;
            LPWSTR msg = nullptr;
            if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
                std::string utf8 = to_utf8(msg);
                CoTaskMemFree(msg);
                if (owner_->on_msg_) owner_->on_msg_(utf8);
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) override {
            if (!args) return S_OK;
            COREWEBVIEW2_PERMISSION_KIND kind{};
            args->get_PermissionKind(&kind);
            if (kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ) {
                args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) override {
            if (!owner_ || !owner_->env_ || !args) return S_OK;
            ICoreWebView2WebResourceRequest* req = nullptr;
            if (FAILED(args->get_Request(&req)) || !req) return S_OK;
            LPWSTR uri = nullptr;
            if (SUCCEEDED(req->get_Uri(&uri)) && uri) {
                static const wchar_t* kPrefix = L"https://app.typecount.local/";
                const size_t kLen = 28;
                if (wcsncmp(uri, kPrefix, kLen) == 0) {
                    std::string rel = to_utf8(std::wstring(uri + kLen));
                    const UiAsset* asset = find_ui_asset(rel);
                    if (asset) {
                        IStream* stream = SHCreateMemStream(asset->data, static_cast<UINT>(asset->size));
                        if (stream) {
                            std::wstring headers = L"Content-Type: " + to_wide(asset->mime) +
                                                   L"\r\nCache-Control: no-store";
                            ICoreWebView2WebResourceResponse* resp = nullptr;
                            if (SUCCEEDED(owner_->env_->CreateWebResourceResponse(
                                    stream, 200, L"OK", headers.c_str(), &resp)) && resp) {
                                args->put_Response(resp);
                                resp->Release();
                            }
                            stream->Release();
                        }
                    } else {
                        ICoreWebView2WebResourceResponse* resp = nullptr;
                        if (SUCCEEDED(owner_->env_->CreateWebResourceResponse(
                                nullptr, 404, L"Not Found", L"Content-Type: text/plain", &resp)) && resp) {
                            args->put_Response(resp);
                            resp->Release();
                        }
                    }
                }
                CoTaskMemFree(uri);
            }
            req->Release();
            return S_OK;
        }

    private:
        std::atomic<ULONG> refs_{1};
        WebViewHost* owner_ = nullptr;
    };

    webview::detail::mswebview2::loader loader_;
    HWND parent_ = nullptr;
    std::wstring user_data_dir_;
    std::string initial_locale_ = "en";
    std::string initial_theme_ = "blue";
    MessageCallback on_msg_;
    ReadyCallback on_ready_;
    std::function<void()> on_runtime_missing_;

    ICoreWebView2Environment* env_ = nullptr;
    ICoreWebView2Controller* controller_ = nullptr;
    ICoreWebView2* webview_ = nullptr;
    ComHandler* handler_ = nullptr;
    std::vector<std::string> pending_scripts_;
    bool visible_ = false;
    bool initializing_ = false;
    bool ready_ = false;
};

} // namespace win
} // namespace tc
