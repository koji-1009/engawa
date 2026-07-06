#include "ConformanceChannel.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "Bootstrap.hpp"

void ConformanceChannel::wire(Bridge* bridge) {
    bridge_ = bridge;
    bridge_->onConfResult = [this](int reqId, bool ok, Json value, Json err) {
        writeResult(reqId, ok, value, err);
    };
    bridge_->onConfEvent = [this](std::string topic, Json payload) {
        writeLine(Json{{"ctl", "event"}, {"topic", topic}, {"payload", payload}});
    };
}

void ConformanceChannel::start() {
    std::thread(&ConformanceChannel::readLoop, this).detach();
}

void ConformanceChannel::emitReady() { writeLine(Json{{"ctl", "ready"}}); }

void ConformanceChannel::emitFloorRejected(const std::string& detected, const std::string& required) {
    writeLine(Json{{"ctl", "floorRejected"}, {"detected", detected}, {"required", required}});
}

// Read stdin as newline-delimited JSON. A control message may exceed 1 MiB (the echo round-trip
// carries the large-message tests over this channel), which std::getline handles.
void ConformanceChannel::readLoop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        Json o = Json::parse(line, nullptr, false);
        if (o.is_discarded() || !o.is_object()) continue;
        try { handle(o); } catch (...) { /* a malformed control message must not kill the channel */ }
    }
}

void ConformanceChannel::handle(const Json& o) {
    std::string ctl = o.value("ctl", std::string());
    int reqId = o.contains("reqId") && o["reqId"].is_number() ? o["reqId"].get<int>() : 0;

    if (ctl == "invoke") {
        std::string cmd = o.value("cmd", std::string());
        Json args = o.contains("args") ? o["args"] : Json(nullptr);
        bridge_->post([this, reqId, cmd, args] { bridge_->relayInvoke(reqId, cmd, args); });
    } else if (ctl == "subscribe") {
        std::string topic = o.value("topic", std::string());
        bridge_->post([this, topic] { bridge_->relaySubscribe(topic); });
    } else if (ctl == "introspect") {
        runScript(reqId, Bootstrap::IntrospectScript);
    } else if (ctl == "frameCheck") {
        runScript(reqId, Bootstrap::FrameCheckScript);
    } else if (ctl == "nonAppCheck") {
        runScript(reqId, Bootstrap::NonAppCheckScript);
    } else if (ctl == "simulateRenderCrash") {
        bridge_->post([this, reqId] { writeResult(reqId, true, bridge_->simulateRenderCrash(), Json(nullptr)); });
    } else if (ctl == "ioPut") {
        std::string url = o.value("url", std::string());
        std::string dataB64 = o.value("dataB64", std::string());
        bridge_->post([this, reqId, url, dataB64] { bridge_->relayIoPut(reqId, url, dataB64); });
    } else if (ctl == "ioGet") {
        std::string url = o.value("url", std::string());
        bridge_->post([this, reqId, url] { bridge_->relayIoGet(reqId, url); });
    } else if (ctl == "quit") {
        fflush(stdout);
        // Hard exit WITHOUT running static destructors: this fires on the background reader thread while
        // gtk_main() and detached process-reader threads are live on other threads; std::exit would tear
        // down globals concurrently with them (a shutdown data race). std::_Exit skips destructors.
        std::_Exit(0);
    }
}

// introspect/frameCheck/nonAppCheck read synchronous page state (no in-page promise), so evaluate
// directly and answer with the parsed value.
void ConformanceChannel::runScript(int reqId, const char* script) {
    std::string s = script;
    bridge_->post([this, reqId, s] {
        bridge_->executeJson(s, [this, reqId](bool ok, Json v) {
            if (ok) writeResult(reqId, true, v, Json(nullptr));
            else writeResult(reqId, false, Json(nullptr), Json{{"code", "EIO"}, {"message", "executeScript failed"}});
        });
    });
}

void ConformanceChannel::writeResult(int reqId, bool ok, const Json& value, const Json& err) {
    Json o{{"ctl", "result"}, {"reqId", reqId}, {"ok", ok}};
    if (ok) o["value"] = value;
    else o["err"] = err;
    writeLine(o);
}

void ConformanceChannel::writeLine(const Json& o) {
    std::string line = o.dump();
    line.push_back('\n');
    std::lock_guard<std::mutex> lk(outMu_);
    fwrite(line.data(), 1, line.size(), stdout);
    fflush(stdout);
}
