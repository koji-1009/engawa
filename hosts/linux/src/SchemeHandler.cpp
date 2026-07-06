#include "SchemeHandler.hpp"

#include <libsoup/soup.h>

#include <cctype>

#include "Json.hpp"
#include "Mime.hpp"
#include "PathUtil.hpp"

namespace {

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

// Split "app://<authority>/<path>?query#frag" into authority + path (query/fragment dropped).
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

bool iequals(const std::string& a, const char* b) { return g_ascii_strcasecmp(a.c_str(), b) == 0; }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper((unsigned char)c));
    return s;
}

// Read a GInputStream (the app://io PUT body) fully into a std::string; NULL stream → empty. `ok` is
// set false on a mid-stream read ERROR (n < 0), distinct from EOF (n == 0), so the caller never
// commits a truncated body as a successful write.
std::string readStream(GInputStream* s, bool& ok) {
    ok = true;
    std::string out;
    if (!s) return out;
    char buf[65536];
    for (;;) {
        gssize n = g_input_stream_read(s, buf, sizeof(buf), nullptr, nullptr);
        if (n < 0) { ok = false; break; }  // read error — do not treat as EOF
        if (n == 0) break;                 // EOF
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

// §5a: opt app://app in on app://io responses so an app document can read io results across the
// distinct-origin boundary.
void appendCors(SoupMessageHeaders* h) {
    soup_message_headers_append(h, "Access-Control-Allow-Origin", SchemeHandler::AppOrigin);
    soup_message_headers_append(h, "Access-Control-Allow-Methods", "GET, PUT");
}

// Finish a request with an in-memory body, a status, a content type, and optional extra headers
// (CSP / CORS). Takes ownership of nothing the caller passes; copies `body` into the stream.
void finish(WebKitURISchemeRequest* request, const std::string& body, int status, const char* reason,
            const char* contentType, SoupMessageHeaders* extra) {
    GInputStream* stream =
        g_memory_input_stream_new_from_data(g_memdup2(body.data(), body.size()), body.size(), g_free);
    WebKitURISchemeResponse* resp = webkit_uri_scheme_response_new(stream, static_cast<gint64>(body.size()));
    webkit_uri_scheme_response_set_status(resp, status, reason);
    webkit_uri_scheme_response_set_content_type(resp, contentType);
    // set_http_headers REPLACES the whole response header block, so Content-Type must live inside it
    // too — otherwise it is dropped on every response that carries extra headers (CSP on assets, CORS
    // on io), and WebKit falls back to a default/empty MIME (breaking strict-MIME module scripts).
    SoupMessageHeaders* headers = extra ? extra : soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    if (!soup_message_headers_get_one(headers, "Content-Type"))
        soup_message_headers_append(headers, "Content-Type", contentType);
    webkit_uri_scheme_response_set_http_headers(resp, headers);  // transfer full
    webkit_uri_scheme_request_finish_with_response(request, resp);
    g_object_unref(resp);
    g_object_unref(stream);
}

void finishText(WebKitURISchemeRequest* request, int status, const char* reason, const std::string& body) {
    finish(request, body, status, reason, "text/plain; charset=utf-8", nullptr);
}

// §5a error envelope: HTTP 200 with { ok:false, err:{ code, message } } (spec/assets.md), plus CORS.
void ioError(WebKitURISchemeRequest* request, const std::string& code, const std::string& message) {
    std::string b = "{\"ok\":false,\"err\":{\"code\":" + Json(code).dump() + ",\"message\":" + Json(message).dump() + "}}";
    SoupMessageHeaders* h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    appendCors(h);
    finish(request, b, 200, "OK", "application/json", h);
}

}  // namespace

SchemeHandler::SchemeHandler(WebKitWebContext* context, std::function<std::string()> liveRoot,
                             std::string csp, IoChannel& io)
    : liveRoot_(std::move(liveRoot)), csp_(std::move(csp)), io_(io) {
    webkit_web_context_register_uri_scheme(context, "app", &SchemeHandler::dispatch, this, nullptr);
    WebKitSecurityManager* sm = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(sm, "app");         // trustworthy origin (secure context)
    webkit_security_manager_register_uri_scheme_as_cors_enabled(sm, "app");   // §5a: cross-origin io fetch obeys CORS
}

void SchemeHandler::dispatch(WebKitURISchemeRequest* request, gpointer self) {
    static_cast<SchemeHandler*>(self)->handle(request);
}

void SchemeHandler::handle(WebKitURISchemeRequest* request) {
    std::string uri = webkit_uri_scheme_request_get_uri(request);
    const char* m = webkit_uri_scheme_request_get_http_method(request);
    std::string method = m ? upper(m) : "GET";

    std::string authority, path;
    parseUri(uri, authority, path);

    if (iequals(authority, "io")) {
        std::string token = path;
        while (!token.empty() && token.front() == '/') token.erase(token.begin());
        handleIo(request, decodePercent(token), method);
        return;
    }
    if (iequals(authority, "app")) {
        handleAsset(request, path);
        return;
    }
    finishText(request, 404, "Not Found", "unknown app authority");
}

void SchemeHandler::handleAsset(WebKitURISchemeRequest* request, const std::string& rawPath) {
    std::string rel = decodePercent(rawPath);
    if (rel.empty() || rel == "/") rel = "/index.html";
    std::string trimmed = rel;
    while (!trimmed.empty() && trimmed.front() == '/') trimmed.erase(trimmed.begin());

    std::error_code ec;
    fsys::path root = fsys::absolute(P(liveRoot_()), ec).lexically_normal();
    fsys::path full = (root / P(trimmed)).lexically_normal();

    if (!Files::isInside(U8(root), U8(full))) {
        finishText(request, 403, "Forbidden", "path escapes asset root");
        return;
    }
    std::string fullU8 = U8(full);
    if (!Files::isFile(fullU8)) {
        finishText(request, 404, "Not Found", "no such asset: " + rel);
        return;
    }
    std::string bytes;
    if (!Files::readAllBytes(fullU8, bytes)) {
        finishText(request, 404, "Not Found", "no such asset: " + rel);
        return;
    }
    // §7.3: the default CSP (plus engawa.json relaxations) on every app asset response.
    SoupMessageHeaders* h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
    soup_message_headers_append(h, "Content-Security-Policy", csp_.c_str());
    finish(request, bytes, 200, "OK", MimeForPath(fullU8).c_str(), h);
}

void SchemeHandler::handleIo(WebKitURISchemeRequest* request, const std::string& token, const std::string& method) {
    if (method == "OPTIONS") {
        SoupMessageHeaders* h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
        appendCors(h);
        soup_message_headers_append(h, "Access-Control-Allow-Headers", "*");
        finish(request, "", 204, "No Content", "text/plain", h);
        return;
    }

    // §5a: status is ALWAYS 200 (except the 204 preflight); success and failure ride the body as a
    // JSON envelope { ok, value } | { ok:false, err:{ code, message } } (spec/assets.md).
    auto t = io_.consume(token);  // single-use (§5a)
    if (!t) {
        ioError(request, "EINVAL", "unknown or consumed io token");
        return;
    }
    try {
        if (method == "PUT" && t->write) {
            GInputStream* bodyStream = webkit_uri_scheme_request_get_http_body(request);  // transfer full
            bool ok = true;
            std::string body = readStream(bodyStream, ok);
            if (bodyStream) g_object_unref(bodyStream);  // own the body stream — was leaked per PUT
            if (!ok) throw EngawaError::io("failed reading io PUT body");
            Files::writeAllBytesAtomic(t->path, body);
            SoupMessageHeaders* h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
            appendCors(h);
            finish(request, "{\"ok\":true,\"value\":{\"bytesWritten\":" + std::to_string(body.size()) + "}}",
                   200, "OK", "application/json", h);
        } else if (method == "GET" && !t->write) {
            std::string bytes;
            if (!Files::readAllBytes(t->path, bytes)) throw EngawaError::io("cannot read: " + t->path);
            SoupMessageHeaders* h = soup_message_headers_new(SOUP_MESSAGE_HEADERS_RESPONSE);
            appendCors(h);
            finish(request, bytes, 200, "OK", "application/octet-stream", h);
        } else {
            ioError(request, "EINVAL", "method does not match the token direction");
        }
    } catch (const std::exception& e) {
        ioError(request, "EIO", e.what());
    }
}
