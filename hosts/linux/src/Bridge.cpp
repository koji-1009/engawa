#include "Bridge.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "Bootstrap.hpp"
#include "Contract.hpp"
#include "EngineFloor.hpp"
#include "PathUtil.hpp"

namespace {

std::string htmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out.push_back(c);
    }
    return out;
}

// Case-insensitive prefix test — a scheme may arrive non-lowercased (e.g. "HTTPS://" via a redirect),
// so the §7 external-navigation guard must not be case-sensitive.
bool startsWithCI(const std::string& s, const char* p) {
    return g_ascii_strncasecmp(s.c_str(), p, strlen(p)) == 0;
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// evaluate_javascript result trampoline for executeJson (introspect/frameCheck/nonAppCheck).
struct EvalCtx {
    std::function<void(bool, Json)> cb;
};
void onEvalDone(GObject* src, GAsyncResult* res, gpointer data) {
    auto* ctx = static_cast<EvalCtx*>(data);
    GError* err = nullptr;
    JSCValue* v = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(src), res, &err);
    if (!v) {
        if (err) g_error_free(err);
        ctx->cb(false, Json(nullptr));
        delete ctx;
        return;
    }
    char* j = jsc_value_to_json(v, 0);
    std::string s = j ? j : "";
    if (j) g_free(j);
    g_object_unref(v);
    Json val = (s.empty() || s == "null") ? Json(nullptr) : Json::parse(s, nullptr, false);
    if (val.is_discarded()) val = Json(nullptr);
    ctx->cb(true, val);
    delete ctx;
}

}  // namespace

Bridge::Bridge(Window& window, const HostOptions& opts, Dispatcher& dispatcher, std::string shellJs,
               IoChannel& io, std::function<std::string()> liveRoot, std::string csp)
    : window_(window), opts_(opts), dispatcher_(dispatcher), shellJs_(std::move(shellJs)), io_(io),
      liveRoot_(std::move(liveRoot)), csp_(std::move(csp)) {}

void Bridge::startWebView() {
    std::string dataDir = U8(P(opts_.dataRoot) / "webkit");
    std::string cacheDir = U8(P(opts_.dataRoot) / "webkit-cache");
    // §10: WebView-managed storage (IndexedDB/localStorage) is cache; the suite wipes it and the app
    // must still boot.
    std::error_code ec;
    if (opts_.wipeStorage && Files::isDir(dataDir)) fsys::remove_all(P(dataDir), ec);
    fsys::create_directories(P(dataDir), ec);
    fsys::create_directories(P(cacheDir), ec);

    WebKitWebsiteDataManager* dm = webkit_website_data_manager_new(
        "base-data-directory", dataDir.c_str(), "base-cache-directory", cacheDir.c_str(), nullptr);
    context_ = webkit_web_context_new_with_website_data_manager(dm);
    g_object_unref(dm);

    // §5: serve app:// ourselves (registers the scheme on the context as secure + CORS-enabled).
    scheme_ = std::make_unique<SchemeHandler>(context_, liveRoot_, csp_, io_);

    // §1/§6/§7.3: install the bridge at document start via the user-script path (CSP-exempt). Runs in
    // ALL frames; the §6 subframe/non-app guards live inside the injected script.
    ucm_ = webkit_user_content_manager_new();
    std::optional<std::string> autotest;
    if (opts_.autotest) {
        std::string upd = opts_.autotestUpdate.value_or("null");
        if (upd.empty()) upd = "null";
        autotest = std::string("{\"update\":") + upd + "}";
    }
    std::string boot = Bootstrap::build(kPlatform, kContractVersion, dispatcher_.capabilities(),
                                        shellJs_, opts_.conformance, autotest);
    WebKitUserScript* script = webkit_user_script_new(
        boot.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm_, script);
    webkit_user_script_unref(script);

    // The host's receive-a-string primitive: window.webkit.messageHandlers.engawa.postMessage(s).
    webkit_user_content_manager_register_script_message_handler(ucm_, "engawa");
    g_signal_connect(ucm_, "script-message-received::engawa", G_CALLBACK(&Bridge::onScriptMessage), this);

    view_ = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", context_,
                                         "user-content-manager", ucm_, nullptr));

    WebKitSettings* settings = webkit_web_view_get_settings(view_);
    webkit_settings_set_enable_developer_extras(settings, FALSE);

    g_signal_connect(view_, "context-menu", G_CALLBACK(&Bridge::onContextMenu), this);
    g_signal_connect(view_, "load-changed", G_CALLBACK(&Bridge::onLoadChanged), this);
    g_signal_connect(view_, "decide-policy", G_CALLBACK(&Bridge::onDecidePolicy), this);
    g_signal_connect(view_, "web-process-terminated", G_CALLBACK(&Bridge::onWebProcessTerminated), this);

    window_.setWebView(GTK_WIDGET(view_));  // realizes the view (runs even offscreen)
    finishInit();
}

void Bridge::finishInit() {
    // §9 engine floor. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected version for the suite.
    std::string detected;
    if (opts_.fakeEngineVersion) {
        detected = *opts_.fakeEngineVersion;
    } else {
        detected = std::to_string(webkit_get_major_version()) + "." +
                   std::to_string(webkit_get_minor_version()) + "." +
                   std::to_string(webkit_get_micro_version());
    }
    engineVersion_ = detected;
    if (EngineFloor::isBelowFloor(detected)) {
        // No partial boot (§9): report and stop — do not navigate, do not signal ready.
        if (onFloorRejected) onFloorRejected(detected, EngineFloor::Required);
        return;
    }

    std::string start = opts_.startUrl.value_or("app://app/");
    webkit_web_view_load_uri(view_, start.c_str());
    post([this] { flush(); });  // drain anything queued during init
}

// ---- receive a string ---------------------------------------------------------------------

void Bridge::onScriptMessage(WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer self) {
    // webkit2gtk-4.1's script-message-received passes a WebKitJavascriptResult, not a bare JSCValue —
    // unwrap it to the posted string.
    if (!result) return;
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    char* s = value ? jsc_value_to_string(value) : nullptr;
    if (s) {
        static_cast<Bridge*>(self)->onWebMessage(s);
        g_free(s);
    }
}

void Bridge::onWebMessage(const std::string& msg) {
    if (msg.empty()) return;
    Json o = Json::parse(msg, nullptr, false);
    if (o.is_discarded() || !o.is_object()) return;
    if (o.contains("__conf")) handleConf(o);
    else if (o.value("t", std::string()) == "req") handleRequest(o);
}

void Bridge::handleConf(const Json& o) {
    std::string kind = o.value("__conf", std::string());
    if (kind == "result") {
        int reqId = o.contains("reqId") && o["reqId"].is_number() ? o["reqId"].get<int>() : 0;
        bool ok = o.value("ok", false);
        Json value = o.contains("value") ? o["value"] : Json(nullptr);
        Json err = o.contains("err") ? o["err"] : Json(nullptr);
        if (onConfResult) onConfResult(reqId, ok, value, err);
    } else if (kind == "event") {
        std::string topic = o.value("topic", std::string());
        Json payload = o.contains("payload") ? o["payload"] : Json(nullptr);
        if (onConfEvent) onConfEvent(topic, payload);
    }
}

void Bridge::handleRequest(const Json& o) {
    Json idNode = o.contains("id") ? o["id"] : Json(0);
    std::string cmd = o.value("cmd", std::string());
    static const Json kNull;  // bind args by reference — avoid deep-copying a large payload (e.g. echo)
    const Json& args = o.contains("args") ? o.at("args") : kNull;

    DispatchResult r = dispatcher_.dispatch(cmd, args);

    Json res;
    res["t"] = "res";
    res["id"] = idNode;
    if (r.ok) {
        res["ok"] = true;
        res["value"] = r.value;
    } else {
        res["ok"] = false;
        res["err"] = Json{{"code", r.code}, {"message", r.message}};
    }
    enqueue(std::move(res));
}

// ---- evaluate a string (delivery, §2.1) ---------------------------------------------------

void Bridge::evalJs(const std::string& script) {
    if (!view_) return;
    webkit_web_view_evaluate_javascript(view_, script.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

void Bridge::emit(const std::string& topic, const Json& payload) {
    Json f;
    f["t"] = "evt";
    f["topic"] = topic;
    f["payload"] = payload;
    enqueue(std::move(f));
}

void Bridge::enqueue(Json frame) {
    bool schedule;
    {
        std::lock_guard<std::mutex> lk(outMu_);
        outbound_.push_back(std::move(frame));
        schedule = !flushScheduled_;
        if (schedule) flushScheduled_ = true;
    }
    if (schedule) window_.post([this] { flush(); });
}

void Bridge::flush() {
    if (!view_) {
        // Not ready to deliver yet: drop the latch WITHOUT draining so the queued frames survive and a
        // later enqueue — or the post-init drain — re-drives delivery.
        std::lock_guard<std::mutex> lk(outMu_);
        flushScheduled_ = false;
        return;
    }
    std::vector<Json> batch;
    {
        std::lock_guard<std::mutex> lk(outMu_);
        batch.swap(outbound_);
        flushScheduled_ = false;
    }
    if (batch.empty()) return;

    // §2.1: keep only the last window.resize event in the batch.
    int last = -1;
    for (int i = 0; i < static_cast<int>(batch.size()); i++)
        if (batch[i].value("t", std::string()) == "evt" && batch[i].value("topic", std::string()) == "window.resize")
            last = i;
    Json arr = Json::array();
    for (int i = 0; i < static_cast<int>(batch.size()); i++) {
        bool isResize = batch[i].value("t", std::string()) == "evt" &&
                        batch[i].value("topic", std::string()) == "window.resize";
        if (!isResize || i == last) arr.push_back(batch[i]);
    }

    std::string arrayJson = arr.dump();
    // shell.js _deliver takes the JSON array as a *string*; embed it as an ASCII JS string literal.
    std::string script = "__shell._deliver(" + Json(arrayJson).dump(-1, ' ', true) + ")";
    evalJs(script);
}

// ---- navigation / crash / menus -----------------------------------------------------------

void Bridge::onLoadChanged(WebKitWebView*, WebKitLoadEvent event, gpointer self) {
    auto* b = static_cast<Bridge*>(self);
    if (event == WEBKIT_LOAD_FINISHED && !b->readySignaled_) {
        b->readySignaled_ = true;
        if (b->onReady) b->onReady();
    }
}

gboolean Bridge::onDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                                WebKitPolicyDecisionType type, gpointer) {
    // §7: a self-contained app never opens an OS-level new window (window.open / target=_blank);
    // external navigation goes only through shellOpen.openExternal. Deny every new-window request.
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;  // response, etc. → default
    auto* nav = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
    WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(nav);
    WebKitURIRequest* req = webkit_navigation_action_get_request(action);
    const char* uri = webkit_uri_request_get_uri(req);
    std::string u = uri ? uri : "";
    // Deny external navigation (§7); shellOpen.openExternal is the sanctioned path. Case-insensitive so
    // a redirect that preserves scheme case (HTTPS://) can't slip past.
    if (startsWithCI(u, "http://") || startsWithCI(u, "https://")) {
        webkit_policy_decision_ignore(decision);
        return TRUE;
    }
    return FALSE;
}

void Bridge::onWebProcessTerminated(WebKitWebView*, WebKitWebProcessTerminationReason reason, gpointer self) {
    // §10: only a genuine renderer crash (or an OOM kill) counts. A host/API-initiated termination
    // (e.g. during shutdown) must not inflate the crash budget or trigger a reload of a dying view.
    if (reason != WEBKIT_WEB_PROCESS_CRASHED && reason != WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT) return;
    auto* b = static_cast<Bridge*>(self);
    auto [count, over] = b->bumpCrash();
    b->emit("app.renderCrashed", Json{{"count", count}});
    if (over) b->showErrorScreen("The app's renderer crashed repeatedly and was stopped.");
    else webkit_web_view_reload(b->view_);
}

gboolean Bridge::onContextMenu(WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) {
    return TRUE;  // no default context menu in a shipped app
}

std::pair<int, bool> Bridge::bumpCrash() {
    long long now = nowMs();
    crashesMs_.push_back(now);
    std::vector<long long> kept;
    for (long long t : crashesMs_)
        if (now - t <= 60000) kept.push_back(t);
    crashesMs_.swap(kept);
    int count = static_cast<int>(crashesMs_.size());
    return {count, count >= 3};
}

Json Bridge::simulateRenderCrash() {
    // §10 conformance hook: no reliable renderer-death trigger, so run the SAME accounting +
    // app.renderCrashed event the real path does. No actual reload — that would break the suite's page.
    auto [count, over] = bumpCrash();
    emit("app.renderCrashed", Json{{"count", count}});
    return Json{{"over", over}};
}

void Bridge::showErrorScreen(const std::string& message) {
    std::string html =
        "<!doctype html><meta charset=\"utf-8\"><body style=\"font:14px sans-serif;padding:2rem\">" +
        htmlEscape(message) + "</body>";
    if (view_) webkit_web_view_load_html(view_, html.c_str(), "app://app/");
}

// ---- conformance relay --------------------------------------------------------------------

void Bridge::executeJson(const std::string& script, std::function<void(bool, Json)> cb) {
    if (!view_) { cb(false, Json(nullptr)); return; }
    auto* ctx = new EvalCtx{std::move(cb)};
    webkit_web_view_evaluate_javascript(view_, script.c_str(), -1, nullptr, nullptr, nullptr, &onEvalDone, ctx);
}

void Bridge::relayInvoke(int reqId, const std::string& cmd, const Json& args) {
    evalJs("window.__engawaConf.invoke(" + std::to_string(reqId) + "," + Json(cmd).dump() + "," + args.dump() + ")");
}

void Bridge::relaySubscribe(const std::string& topic) {
    evalJs("window.__engawaConf.subscribe(" + Json(topic).dump() + ")");
}

void Bridge::relayIoPut(int reqId, const std::string& url, const std::string& dataB64) {
    evalJs("window.__engawaConf.ioPut(" + std::to_string(reqId) + "," + Json(url).dump() + "," + Json(dataB64).dump() + ")");
}

void Bridge::relayIoGet(int reqId, const std::string& url) {
    evalJs("window.__engawaConf.ioGet(" + std::to_string(reqId) + "," + Json(url).dump() + ")");
}
