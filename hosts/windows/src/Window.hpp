#pragma once
// The application window as a raw Win32 window hosting a CoreWebView2Controller — a small native
// program, no UI framework. Exposes the surface the window adapter and bridge use: the window ops
// (applyClientSize/applyTitle/…), post() to marshal work onto the UI thread, and the user-driven
// hooks (onUserCloseAttempt/onUserResized) plus focus/blur via the emitter.
#include <windows.h>
#include <wrl.h>

#include <functional>
#include <mutex>
#include <vector>

#include "Adapter.hpp"
#include "WebView2.h"

class Window {
public:
    explicit Window(bool hidden);

    HWND hwnd() const { return hwnd_; }

    // Set once the WebView2 controller exists, so WM_SIZE can keep it filling the client area.
    void setController(ICoreWebView2Controller* c) { controller_ = c; }

    // Wiring from the window adapter / bridge.
    IEventEmitter* emitter = nullptr;         // window.focus / window.blur
    std::function<bool()> onUserCloseAttempt;  // §4.2 close gate; true = intercept (keep the window)
    std::function<void(int, int)> onUserResized;

    // Marshal an action onto the UI thread (the control channel runs on a background reader thread).
    void post(std::function<void()> fn);

    // The blocking GetMessage pump (contract §1: WebView2 needs a running message loop).
    void runMessageLoop();

    // ---- window ops (called on the UI thread) ---------------------------------------------
    void applyClientSize(int w, int h);
    void applyTitle(const std::string& title);
    void applyResizable(bool resizable);
    void doMinimize();
    void doMaximize();
    void closeWindow();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    bool handle(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);
    void drainActions();

    HWND hwnd_ = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    std::mutex actionMu_;
    std::vector<std::function<void()>> actions_;
    bool programmaticResize_ = false;
};
