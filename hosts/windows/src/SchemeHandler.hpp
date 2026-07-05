#pragma once
// Serves the `app://` scheme (contract §5, §5a; spec/assets.md). Two origins, never colliding
// because they are different authorities:
//   app://app/<path> — the app's asset tree, with the §7.3 CSP on every response.
//   app://io/<token> — the §5a binary channel; distinct origin, so its responses carry CORS headers
//                      opting app://app in. Never http://localhost (contract §5).
#include <wrl.h>

#include <functional>
#include <string>

#include "IoChannel.hpp"
#include "WebView2.h"

class SchemeHandler {
public:
    static constexpr const char* AppOrigin = "app://app";

    SchemeHandler(ICoreWebView2Environment* env, std::function<std::string()> liveRoot,
                  std::string csp, IoChannel& io);

    void handle(ICoreWebView2WebResourceRequestedEventArgs* args);

private:
    void handleAsset(ICoreWebView2WebResourceRequestedEventArgs* args, const std::string& path);
    void handleIo(ICoreWebView2WebResourceRequestedEventArgs* args, const std::string& token,
                  const std::string& method, ICoreWebView2WebResourceRequest* request);

    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> makeResponse(
        const std::string& body, int status, const wchar_t* reason, const std::string& headers);
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> emptyResponse(
        int status, const wchar_t* reason, const std::string& headers);
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> text(int status, const wchar_t* reason,
                                                                   const std::string& body);
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> json(int status, const wchar_t* reason,
                                                                   const std::string& body);
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> ioError(const std::string& code,
                                                                      const std::string& message);

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> env_;
    std::function<std::string()> liveRoot_;  // update may swap the served slot at runtime (§8)
    std::string csp_;
    IoChannel& io_;
};
