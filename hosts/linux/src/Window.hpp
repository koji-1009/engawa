#pragma once
// The application window as a GtkWindow hosting a WebKitWebView — a small native program, no extra UI
// framework. Exposes the surface the window adapter and bridge use: the window ops
// (applyClientSize/applyTitle/…), post() to marshal work onto the GTK main thread (the control
// channel runs on a background reader thread), and the user-driven hooks (onUserCloseAttempt/
// onUserResized) plus focus/blur via the emitter. Under conformance/autotest the window is offscreen.
#include <gtk/gtk.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Adapter.hpp"

class Window {
public:
    explicit Window(bool hidden);

    GtkWidget* gtkWindow() const { return window_; }

    // Host the WebKitWebView widget and realize the window (so the view runs even offscreen).
    void setWebView(GtkWidget* webview);

    // Wiring from the window adapter / bridge.
    IEventEmitter* emitter = nullptr;          // window.focus / window.blur
    std::function<bool()> onUserCloseAttempt;  // §4.2 close gate; true = intercept (keep the window)
    std::function<void(int, int)> onUserResized;

    // Marshal an action onto the GTK main thread (g_idle_add is safe to call from any thread).
    void post(std::function<void()> fn);

    // The blocking gtk_main() pump (contract §1: WebKit needs a running main loop).
    void runMessageLoop();

    // ---- window ops (called on the main thread) -------------------------------------------
    void applyClientSize(int w, int h);
    void applyTitle(const std::string& title);
    void applyResizable(bool resizable);
    void doMinimize();
    void doMaximize();
    void closeWindow();

private:
    void drainActions();
    static gboolean onIdle(gpointer self);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer self);
    static void onDestroy(GtkWidget*, gpointer self);
    static gboolean onConfigure(GtkWidget*, GdkEventConfigure*, gpointer self);
    static gboolean onFocusIn(GtkWidget*, GdkEventFocus*, gpointer self);
    static gboolean onFocusOut(GtkWidget*, GdkEventFocus*, gpointer self);

    GtkWidget* window_ = nullptr;
    bool quit_ = false;
    std::mutex actionMu_;
    std::vector<std::function<void()>> actions_;
    bool suppressConfigure_ = false;  // swallow the configure-event a programmatic resize triggers
    int lastW_ = 1024, lastH_ = 720;
};
