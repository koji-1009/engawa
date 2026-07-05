#pragma once
// The host's half of the two primitives (contract §1): receive a string (WebMessageReceived) and
// evaluate a string (ExecuteScript of __shell._deliver). Everything protocol-shaped lives in
// shell.js; the host only pumps strings, batches delivery (§2.1), and dispatches to adapters. Bridge
// also owns the WebView2 bring-up (environment → controller → injection → navigate).
#include <wrl.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Adapter.hpp"
#include "Dispatcher.hpp"
#include "HostOptions.hpp"
#include "IoChannel.hpp"
#include "SchemeHandler.hpp"
#include "WebView2.h"
#include "Window.hpp"

class Bridge : public IEventEmitter {
public:
    Bridge(Window& window, const HostOptions& opts, Dispatcher& dispatcher, std::string shellJs,
           IoChannel& io, std::function<std::string()> liveRoot, std::string csp);

    // Kick off the WebView2 environment/controller/injection chain (called on the UI thread).
    void startWebView();

    // IEventEmitter — enqueue an evt frame for the next delivery tick.
    void emit(const std::string& topic, const Json& payload) override;

    void post(std::function<void()> fn) { window_.post(std::move(fn)); }

    // Control-channel relays (driven from ConformanceChannel on the UI thread).
    void executeJson(const std::string& script, std::function<void(bool ok, Json value)> cb);
    void relayInvoke(int reqId, const std::string& cmd, const Json& args);
    void relaySubscribe(const std::string& topic);
    void relayIoPut(int reqId, const std::string& url, const std::string& dataB64);
    void relayIoGet(int reqId, const std::string& url);
    Json simulateRenderCrash();
    void showErrorScreen(const std::string& message);

    const std::string& engineVersion() const { return engineVersion_; }

    // Wiring set by main / ConformanceChannel.
    std::function<void()> onReady;
    std::function<void(std::string detected, std::string required)> onFloorRejected;
    std::function<void(int reqId, bool ok, Json value, Json err)> onConfResult;
    std::function<void(std::string topic, Json payload)> onConfEvent;

private:
    HRESULT onEnvironmentCreated(HRESULT result, ICoreWebView2Environment* env);
    HRESULT onControllerCreated(HRESULT result, ICoreWebView2Controller* controller);
    void finishInit();
    void registerHandlersAndNavigate();

    HRESULT onWebMessage(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args);
    void handleConf(const Json& o);
    void handleRequest(const Json& o);
    HRESULT onProcessFailed(ICoreWebView2* sender, ICoreWebView2ProcessFailedEventArgs* args);
    HRESULT onNavigationStarting(ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args);
    HRESULT onNavigationCompleted(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args);

    void enqueue(Json frame);
    void flush();
    std::pair<int, bool> bumpCrash();

    Window& window_;
    const HostOptions& opts_;
    Dispatcher& dispatcher_;
    std::string shellJs_;
    IoChannel& io_;
    std::function<std::string()> liveRoot_;
    std::string csp_;

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> core_;
    std::unique_ptr<SchemeHandler> scheme_;

    std::mutex outMu_;
    std::vector<Json> outbound_;
    bool flushScheduled_ = false;

    std::vector<long long> crashesMs_;  // renderer-crash timestamps (§10)
    bool readySignaled_ = false;
    std::string engineVersion_;
};
