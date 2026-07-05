#pragma once
// The stdio control channel the conformance runner drives (conformance/hosts/windows/driver.js).
// Newline-delimited JSON, mirroring the macOS driver's protocol so ONE suite runs on both hosts:
//   driver → host : { ctl:'invoke'|'subscribe'|'introspect'|'frameCheck'|'nonAppCheck'|'ioPut'|'ioGet'
//                     |'simulateRenderCrash'|'quit', ... }
//   host → driver : { ctl:'ready' } | { ctl:'result', reqId, ok, value|err } | { ctl:'event', topic, payload }
//                    | { ctl:'floorRejected', detected, required }
// invoke/subscribe/io go through the REAL in-page shell.js (Bridge relays them); the page reports
// results back over chrome.webview, and Bridge's onConf* callbacks land here.
#include <mutex>
#include <string>

#include "Bridge.hpp"

class ConformanceChannel {
public:
    void wire(Bridge* bridge);
    void start();  // spawn the stdin reader thread

    void emitReady();
    void emitFloorRejected(const std::string& detected, const std::string& required);

private:
    void readLoop();
    void handle(const Json& o);
    void runScript(int reqId, const char* script);
    void writeResult(int reqId, bool ok, const Json& value, const Json& err);
    void writeLine(const Json& o);

    Bridge* bridge_ = nullptr;
    std::mutex outMu_;
};
