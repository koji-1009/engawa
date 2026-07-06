#include "SchemeHandler.hpp"

#include <shlwapi.h>

#include <cctype>

#include "Json.hpp"
#include "Mime.hpp"
#include "PathUtil.hpp"
#include "Utf.hpp"

using Microsoft::WRL::ComPtr;

namespace {

// Percent-decode a URL path component (Uri.UnescapeDataString equivalent for the asset path).
std::string decodePercent(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            auto hex = [](char c) { return c <= '9' ? c - '0' : (std::tolower(c) - 'a' + 10); };
            out.push_back(static_cast<char>((hex(s[i + 1]) << 4) | hex(s[i + 2])));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Split "app://<authority>/<path>?query#frag" into authority + decoded path (query/fragment dropped).
void parseUri(const std::string& uri, std::string& authority, std::string& path) {
    authority.clear();
    path.clear();
    auto scheme = uri.find("://");
    if (scheme == std::string::npos) return;
    size_t a = scheme + 3;
    size_t slash = uri.find('/', a);
    if (slash == std::string::npos) {
        authority = uri.substr(a);
        return;
    }
    authority = uri.substr(a, slash - a);
    std::string rest = uri.substr(slash);
    auto cut = rest.find_first_of("?#");
    if (cut != std::string::npos) rest = rest.substr(0, cut);
    path = rest;
}

bool iequals(const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper((unsigned char)c));
    return s;
}

std::string readStream(IStream* s) {
    std::string out;
    if (!s) return out;
    char buf[65536];
    for (;;) {
        ULONG n = 0;  // fresh each iteration — never carries a stale count into the append
        HRESULT hr = s->Read(buf, sizeof(buf), &n);
        if (n > 0) out.append(buf, n);
        if (hr != S_OK || n == 0) break;  // S_FALSE / EOF / error, or a zero-length read
    }
    return out;
}

// §5a: engines differ on whether custom-scheme URLs share an origin; opt app://app in so an app
// document can read io results regardless of the engine's policy.
void appendCors(std::string& h) {
    h += "Access-Control-Allow-Origin: ";
    h += SchemeHandler::AppOrigin;
    h += "\r\n";
    h += "Access-Control-Allow-Methods: GET, PUT\r\n";
}

}  // namespace

SchemeHandler::SchemeHandler(ICoreWebView2Environment* env, std::function<std::string()> liveRoot,
                             std::string csp, IoChannel& io)
    : env_(env), liveRoot_(std::move(liveRoot)), csp_(std::move(csp)), io_(io) {}

void SchemeHandler::handle(ICoreWebView2WebResourceRequestedEventArgs* args) {
    ComPtr<ICoreWebView2WebResourceRequest> request;
    if (FAILED(args->get_Request(&request)) || !request) {
        args->put_Response(text(400, L"Bad Request", "bad request").Get());
        return;
    }
    LPWSTR uriRaw = nullptr;
    request->get_Uri(&uriRaw);
    std::string uri = ToUtf8(uriRaw);
    if (uriRaw) CoTaskMemFree(uriRaw);

    std::string authority, path;
    parseUri(uri, authority, path);

    if (iequals(authority, "io")) {
        LPWSTR methodRaw = nullptr;
        request->get_Method(&methodRaw);
        std::string method = methodRaw ? upper(ToUtf8(methodRaw)) : "GET";
        if (methodRaw) CoTaskMemFree(methodRaw);
        std::string token = path;
        while (!token.empty() && (token.front() == '/')) token.erase(token.begin());
        handleIo(args, decodePercent(token), method, request.Get());
        return;
    }
    if (iequals(authority, "app")) {
        handleAsset(args, path);
        return;
    }
    args->put_Response(text(404, L"Not Found", "unknown app authority").Get());
}

void SchemeHandler::handleAsset(ICoreWebView2WebResourceRequestedEventArgs* args, const std::string& rawPath) {
    std::string rel = decodePercent(rawPath);
    if (rel.empty() || rel == "/") rel = "/index.html";
    std::string trimmed = rel;
    while (!trimmed.empty() && (trimmed.front() == '/' || trimmed.front() == '\\')) trimmed.erase(trimmed.begin());

    std::error_code ec;
    fsys::path root = fsys::absolute(P(liveRoot_()), ec).lexically_normal();
    fsys::path full = (root / P(trimmed)).lexically_normal();

    if (!Files::isInside(U8(root), U8(full))) {
        args->put_Response(text(403, L"Forbidden", "path escapes asset root").Get());
        return;
    }
    std::string fullU8 = U8(full);
    if (!Files::isFile(fullU8)) {
        args->put_Response(text(404, L"Not Found", "no such asset: " + rel).Get());
        return;
    }
    // Stream the asset straight from disk (WebView2 pulls lazily): no whole-file read into memory and
    // no second copy into a mem-stream on the message-pump thread. STGM_SHARE_DENY_WRITE is safe for
    // bundle assets (unlike the app://io GET path, which must read-then-close to release the handle).
    std::error_code sizeEc;
    auto fsize = fsys::file_size(full, sizeEc);
    Microsoft::WRL::ComPtr<IStream> stream;
    if (sizeEc ||
        FAILED(SHCreateStreamOnFileEx(full.wstring().c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                      FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream)) ||
        !stream) {
        args->put_Response(text(404, L"Not Found", "no such asset: " + rel).Get());
        return;
    }
    std::string h = "Content-Type: " + MimeForPath(fullU8) + "\r\n";
    h += "Content-Length: " + std::to_string(fsize) + "\r\n";
    // §7.3: the default CSP (plus engawa.json relaxations) on every app asset response.
    h += "Content-Security-Policy: " + csp_ + "\r\n";
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> resp;
    env_->CreateWebResourceResponse(stream.Get(), 200, L"OK", ToWide(h).c_str(), &resp);
    args->put_Response(resp.Get());
}

void SchemeHandler::handleIo(ICoreWebView2WebResourceRequestedEventArgs* args, const std::string& token,
                             const std::string& method, ICoreWebView2WebResourceRequest* request) {
    if (method == "OPTIONS") {
        std::string h;
        appendCors(h);
        h += "Access-Control-Allow-Headers: *\r\n";
        h += "Content-Length: 0\r\n";
        args->put_Response(emptyResponse(204, L"No Content", h).Get());
        return;
    }

    // §5a: status is ALWAYS 200 (except the 204 preflight); success and failure ride the body as a
    // JSON envelope { ok, value } | { ok:false, err:{ code, message } } (spec/assets.md).
    auto t = io_.consume(token);  // single-use (§5a)
    if (!t) {
        args->put_Response(ioError("EINVAL", "unknown or consumed io token").Get());
        return;
    }
    try {
        if (method == "PUT" && t->write) {
            ComPtr<IStream> content;
            request->get_Content(&content);
            std::string body = readStream(content.Get());
            Files::writeAllBytesAtomic(t->path, body);
            args->put_Response(
                json(200, L"OK", "{\"ok\":true,\"value\":{\"bytesWritten\":" + std::to_string(body.size()) + "}}").Get());
        } else if (method == "GET" && !t->write) {
            std::string bytes;
            if (!Files::readAllBytes(t->path, bytes)) throw EngawaError::io("cannot read: " + t->path);
            std::string h = "Content-Type: application/octet-stream\r\n";
            h += "Content-Length: " + std::to_string(bytes.size()) + "\r\n";
            appendCors(h);
            args->put_Response(makeResponse(bytes, 200, L"OK", h).Get());
        } else {
            args->put_Response(ioError("EINVAL", "method does not match the token direction").Get());
        }
    } catch (const std::exception& e) {
        args->put_Response(ioError("EIO", e.what()).Get());
    }
}

ComPtr<ICoreWebView2WebResourceResponse> SchemeHandler::makeResponse(const std::string& body, int status,
                                                                     const wchar_t* reason,
                                                                     const std::string& headers) {
    ComPtr<IStream> stream =
        SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()), static_cast<UINT>(body.size()));
    ComPtr<ICoreWebView2WebResourceResponse> resp;
    env_->CreateWebResourceResponse(stream.Get(), status, reason, ToWide(headers).c_str(), &resp);
    return resp;
}

ComPtr<ICoreWebView2WebResourceResponse> SchemeHandler::emptyResponse(int status, const wchar_t* reason,
                                                                      const std::string& headers) {
    ComPtr<ICoreWebView2WebResourceResponse> resp;
    env_->CreateWebResourceResponse(nullptr, status, reason, ToWide(headers).c_str(), &resp);
    return resp;
}

ComPtr<ICoreWebView2WebResourceResponse> SchemeHandler::text(int status, const wchar_t* reason,
                                                             const std::string& body) {
    std::string h = "Content-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n";
    return makeResponse(body, status, reason, h);
}

ComPtr<ICoreWebView2WebResourceResponse> SchemeHandler::json(int status, const wchar_t* reason,
                                                             const std::string& body) {
    std::string h = "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    appendCors(h);
    return makeResponse(body, status, reason, h);
}

ComPtr<ICoreWebView2WebResourceResponse> SchemeHandler::ioError(const std::string& code,
                                                                const std::string& message) {
    // §5a error envelope: HTTP 200 with { ok:false, err:{ code, message } } (spec/assets.md).
    std::string body = "{\"ok\":false,\"err\":{\"code\":" + Json(code).dump() +
                       ",\"message\":" + Json(message).dump() + "}}";
    return json(200, L"OK", body);
}
