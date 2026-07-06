#pragma once
// Serves the `app://` scheme (contract §5, §5a; spec/assets.md) via WebKitGTK's custom-URI-scheme
// path. Two origins, never colliding because they are different authorities:
//   app://app/<path> — the app's asset tree, with the §7.3 CSP on every response.
//   app://io/<token> — the §5a binary channel; distinct origin, so its responses carry CORS headers
//                      opting app://app in. Never http://localhost (contract §5).
// The scheme is registered on the WebKitWebContext (as secure + CORS-enabled) at construction, before
// any web view loads.
#include <webkit2/webkit2.h>

#include <functional>
#include <string>

#include "IoChannel.hpp"

class SchemeHandler {
public:
    static constexpr const char* AppOrigin = "app://app";

    SchemeHandler(WebKitWebContext* context, std::function<std::string()> liveRoot, std::string csp,
                  IoChannel& io);

private:
    static void dispatch(WebKitURISchemeRequest* request, gpointer self);
    void handle(WebKitURISchemeRequest* request);
    void handleAsset(WebKitURISchemeRequest* request, const std::string& path);
    void handleIo(WebKitURISchemeRequest* request, const std::string& token, const std::string& method);

    std::function<std::string()> liveRoot_;  // update may swap the served slot at runtime (§8)
    std::string csp_;
    IoChannel& io_;
};
