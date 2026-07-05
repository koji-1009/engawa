#include "Bridge.hpp"

#include <windows.h>

#include "Bootstrap.hpp"
#include "Contract.hpp"
#include "EngineFloor.hpp"
#include "PathUtil.hpp"
#include "Utf.hpp"
#include "WebView2EnvironmentOptions.h"

using namespace Microsoft::WRL;

namespace {

// BrowserVersionString can be "149.0.4022.98 dev"; keep the leading dotted-numeric part.
std::string cleanVersion(const std::string& v) {
    size_t a = v.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    std::string s = v.substr(a);
    auto sp = s.find(' ');
    return sp == std::string::npos ? s : s.substr(0, sp);
}

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

bool startsWithCI(const std::string& s, const char* p) { return _strnicmp(s.c_str(), p, strlen(p)) == 0; }

}  // namespace

Bridge::Bridge(Window& window, const HostOptions& opts, Dispatcher& dispatcher, std::string shellJs,
               IoChannel& io, std::function<std::string()> liveRoot, std::string csp)
    : window_(window), opts_(opts), dispatcher_(dispatcher), shellJs_(std::move(shellJs)), io_(io),
      liveRoot_(std::move(liveRoot)), csp_(std::move(csp)) {}

void Bridge::startWebView() {
    std::string udf = U8(P(opts_.dataRoot) / L"webview2");
    // §10: WebView-managed storage is cache; the suite wipes it and the app must still boot.
    std::error_code ec;
    if (opts_.wipeStorage && Files::isDir(udf)) fsys::remove_all(P(udf), ec);
    fsys::create_directories(P(udf), ec);

    auto options = Make<CoreWebView2EnvironmentOptions>();
    auto reg = Make<CoreWebView2CustomSchemeRegistration>(L"app");
    reg->put_TreatAsSecure(TRUE);          // app:// is a trustworthy origin (secure context)
    reg->put_HasAuthorityComponent(TRUE);  // so app://app and app://io are DISTINCT origins (§5a)
    const WCHAR* origins[] = {L"app://app", L"app://io"};
    reg->SetAllowedOrigins(2, origins);
    ComPtr<ICoreWebView2EnvironmentOptions4> options4;
    options.As(&options4);
    ICoreWebView2CustomSchemeRegistration* regs[] = {reg.Get()};
    options4->SetCustomSchemeRegistrations(1, regs);

    std::wstring udfW = ToWide(udf);
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, udfW.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT r, ICoreWebView2Environment* e) { return onEnvironmentCreated(r, e); })
            .Get());
    if (FAILED(hr)) {
        fputs("engawa host: CreateCoreWebView2EnvironmentWithOptions failed (runtime missing?)\n", stderr);
        ExitProcess(1);
    }
}

HRESULT Bridge::onEnvironmentCreated(HRESULT result, ICoreWebView2Environment* env) {
    if (FAILED(result) || !env) { ExitProcess(1); }
    env_ = env;
    scheme_ = std::make_unique<SchemeHandler>(env_.Get(), liveRoot_, csp_, io_);
    return env_->CreateCoreWebView2Controller(
        window_.hwnd(),
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this](HRESULT r, ICoreWebView2Controller* c) { return onControllerCreated(r, c); })
            .Get());
}

HRESULT Bridge::onControllerCreated(HRESULT result, ICoreWebView2Controller* controller) {
    if (FAILED(result) || !controller) { ExitProcess(1); }
    controller_ = controller;
    window_.setController(controller_.Get());
    RECT bounds{};
    if (GetClientRect(window_.hwnd(), &bounds)) controller_->put_Bounds(bounds);
    controller_->get_CoreWebView2(&core_);
    finishInit();
    return S_OK;
}

void Bridge::finishInit() {
    // §9 engine floor. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected version for the suite.
    std::string detected;
    if (opts_.fakeEngineVersion) {
        detected = *opts_.fakeEngineVersion;
    } else {
        LPWSTR v = nullptr;
        env_->get_BrowserVersionString(&v);
        detected = cleanVersion(ToUtf8(v));
        if (v) CoTaskMemFree(v);
    }
    engineVersion_ = detected;
    if (EngineFloor::isBelowFloor(detected)) {
        // No partial boot (§9): report and stop — do not navigate, do not signal ready.
        if (onFloorRejected) onFloorRejected(detected, EngineFloor::Required);
        return;
    }

    // §5: serve app:// ourselves. Filter the whole scheme; the handler splits app/io authorities.
    core_->AddWebResourceRequestedFilter(L"app://*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    EventRegistrationToken tok{};
    core_->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* e) {
                scheme_->handle(e);
                return S_OK;
            })
            .Get(),
        &tok);

    // §1/§6/§7.3: install the bridge at document start via the user-script path (CSP-exempt).
    std::optional<std::string> autotest;
    if (opts_.autotest) {
        std::string upd = opts_.autotestUpdate.value_or("null");
        if (upd.empty()) upd = "null";
        autotest = std::string("{\"update\":") + upd + "}";
    }
    std::string boot = Bootstrap::build(kPlatform, kContractVersion, dispatcher_.capabilities(),
                                        shellJs_, opts_.conformance, autotest);
    core_->AddScriptToExecuteOnDocumentCreated(
        ToWide(boot).c_str(),
        Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
            [this](HRESULT, LPCWSTR) {
                registerHandlersAndNavigate();
                return S_OK;
            })
            .Get());
}

void Bridge::registerHandlersAndNavigate() {
    EventRegistrationToken tok{};
    core_->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2* s, ICoreWebView2WebMessageReceivedEventArgs* a) { return onWebMessage(s, a); })
            .Get(),
        &tok);
    core_->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
            [this](ICoreWebView2* s, ICoreWebView2ProcessFailedEventArgs* a) { return onProcessFailed(s, a); })
            .Get(),
        &tok);

    // Harden the shell: no default context menu in a shipped app; external navigation is the
    // sanctioned shellOpen path (§7), so deny top-level http(s) here too.
    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(core_->get_Settings(&settings)) && settings)
        settings->put_AreDefaultContextMenusEnabled(FALSE);
    core_->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2* s, ICoreWebView2NavigationStartingEventArgs* a) { return onNavigationStarting(s, a); })
            .Get(),
        &tok);
    core_->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* s, ICoreWebView2NavigationCompletedEventArgs* a) { return onNavigationCompleted(s, a); })
            .Get(),
        &tok);

    std::string start = opts_.startUrl.value_or("app://app/");
    core_->Navigate(ToWide(start).c_str());
    post([this] { flush(); });  // drain anything queued during init
}

// ---- receive a string ---------------------------------------------------------------------

HRESULT Bridge::onWebMessage(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) {
    LPWSTR raw = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
    std::string msg = ToUtf8(raw);
    CoTaskMemFree(raw);
    if (msg.empty()) return S_OK;

    Json o = Json::parse(msg, nullptr, false);
    if (o.is_discarded() || !o.is_object()) return S_OK;

    if (o.contains("__conf")) handleConf(o);
    else if (o.value("t", std::string()) == "req") handleRequest(o);
    return S_OK;
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
    if (!core_) {
        // Not ready to deliver yet: drop the latch WITHOUT draining so the queued frames survive and
        // a later enqueue — or the post-init drain — re-drives delivery.
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
    core_->ExecuteScript(ToWide(script).c_str(), nullptr);
}

// ---- navigation / crash -------------------------------------------------------------------

HRESULT Bridge::onNavigationStarting(ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) {
    LPWSTR uriRaw = nullptr;
    args->get_Uri(&uriRaw);
    std::string uri = ToUtf8(uriRaw);
    if (uriRaw) CoTaskMemFree(uriRaw);
    // Deny top-level external navigation (§7); shellOpen.openExternal is the sanctioned path.
    if (startsWithCI(uri, "http://") || startsWithCI(uri, "https://")) args->put_Cancel(TRUE);
    return S_OK;
}

HRESULT Bridge::onNavigationCompleted(ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) {
    if (readySignaled_) return S_OK;
    readySignaled_ = true;
    if (onReady) onReady();
    return S_OK;
}

HRESULT Bridge::onProcessFailed(ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) {
    COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
    args->get_ProcessFailedKind(&kind);
    if (kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED &&
        kind != COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE)
        return S_OK;

    auto [count, over] = bumpCrash();
    emit("app.renderCrashed", Json{{"count", count}});
    if (over) showErrorScreen("The app's renderer crashed repeatedly and was stopped.");
    else core_->Reload();
    return S_OK;
}

std::pair<int, bool> Bridge::bumpCrash() {
    long long now = static_cast<long long>(GetTickCount64());
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
    if (core_) core_->NavigateToString(ToWide(html).c_str());
}

// ---- conformance relay --------------------------------------------------------------------

void Bridge::executeJson(const std::string& script, std::function<void(bool, Json)> cb) {
    if (!core_) { cb(false, Json(nullptr)); return; }
    core_->ExecuteScript(
        ToWide(script).c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [cb](HRESULT hr, LPCWSTR resultJson) -> HRESULT {
                if (FAILED(hr)) { cb(false, Json(nullptr)); return S_OK; }
                std::string j = ToUtf8(resultJson);
                Json v = (j.empty() || j == "null") ? Json(nullptr) : Json::parse(j, nullptr, false);
                if (v.is_discarded()) v = Json(nullptr);
                cb(true, v);
                return S_OK;
            })
            .Get());
}

void Bridge::relayInvoke(int reqId, const std::string& cmd, const Json& args) {
    std::string call = "window.__engawaConf.invoke(" + std::to_string(reqId) + "," + Json(cmd).dump() +
                       "," + args.dump() + ")";
    core_->ExecuteScript(ToWide(call).c_str(), nullptr);
}

void Bridge::relaySubscribe(const std::string& topic) {
    std::string call = "window.__engawaConf.subscribe(" + Json(topic).dump() + ")";
    core_->ExecuteScript(ToWide(call).c_str(), nullptr);
}

void Bridge::relayIoPut(int reqId, const std::string& url, const std::string& dataB64) {
    std::string call = "window.__engawaConf.ioPut(" + std::to_string(reqId) + "," + Json(url).dump() +
                       "," + Json(dataB64).dump() + ")";
    core_->ExecuteScript(ToWide(call).c_str(), nullptr);
}

void Bridge::relayIoGet(int reqId, const std::string& url) {
    std::string call = "window.__engawaConf.ioGet(" + std::to_string(reqId) + "," + Json(url).dump() + ")";
    core_->ExecuteScript(ToWide(call).c_str(), nullptr);
}
