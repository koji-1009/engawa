#pragma once
// The host's half of the two primitives (contract §1): receive a string (a webkit script-message) and
// evaluate a string (webkit_web_view_evaluate_javascript of __shell._deliver). Everything
// protocol-shaped lives in shell.js; the host only pumps strings, batches delivery (§2.1), and
// dispatches to adapters. Bridge also owns the WebKitWebView bring-up (context → scheme → user script
// → message handler → view → navigate).
#include <webkit2/webkit2.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Adapter.hpp"
#include "Dispatcher.hpp"
#include "HostOptions.hpp"
#include "IoChannel.hpp"
#include "SchemeHandler.hpp"
#include "Window.hpp"

class Bridge : public IEventEmitter {
public:
    Bridge(Window& window, const HostOptions& opts, Dispatcher& dispatcher, std::string shellJs,
           IoChannel& io, std::function<std::string()> liveRoot, std::string csp);

    // Build the WebKit context/view/injection chain and navigate (called on the main thread).
    void startWebView();

    // IEventEmitter — enqueue an evt frame for the next delivery tick.
    void emit(const std::string& topic, const Json& payload) override;

    void post(std::function<void()> fn) { window_.post(std::move(fn)); }

    // Control-channel relays (driven from ConformanceChannel on the main thread).
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
    void finishInit();
    void onWebMessage(const std::string& msg);
    void handleConf(const Json& o);
    void handleRequest(const Json& o);
    void evalJs(const std::string& script);

    void enqueue(Json frame);
    void flush();
    std::pair<int, bool> bumpCrash();

    // GTK/WebKit signal trampolines (self is the user_data).
    static void onScriptMessage(WebKitUserContentManager* ucm, WebKitJavascriptResult* result, gpointer self);
    static void onLoadChanged(WebKitWebView* view, WebKitLoadEvent event, gpointer self);
    static gboolean onDecidePolicy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                   WebKitPolicyDecisionType type, gpointer self);
    static void onWebProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason, gpointer self);
    static gboolean onContextMenu(WebKitWebView* view, WebKitContextMenu* menu, GdkEvent* event,
                                  WebKitHitTestResult* hit, gpointer self);

    Window& window_;
    const HostOptions& opts_;
    Dispatcher& dispatcher_;
    std::string shellJs_;
    IoChannel& io_;
    std::function<std::string()> liveRoot_;
    std::string csp_;

    WebKitWebContext* context_ = nullptr;
    WebKitUserContentManager* ucm_ = nullptr;
    WebKitWebView* view_ = nullptr;
    std::unique_ptr<SchemeHandler> scheme_;

    std::mutex outMu_;
    std::vector<Json> outbound_;
    bool flushScheduled_ = false;

    std::vector<long long> crashesMs_;  // renderer-crash timestamps (§10)
    bool readySignaled_ = false;
    std::string engineVersion_;
};
