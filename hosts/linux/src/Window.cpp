#include "Window.hpp"

namespace {
constexpr int kDefaultW = 1024, kDefaultH = 720;
}  // namespace

Window::Window(bool hidden) {
    // Offscreen for the suite / autotest gate (realizes the webview so it loads + runs JS without a
    // visible window); a normal toplevel otherwise. GtkOffscreenWindow is a GtkWindow subclass, so the
    // gtk_window_* ops below apply to both.
    window_ = hidden ? gtk_offscreen_window_new() : gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window_), kDefaultW, kDefaultH);
    gtk_window_set_title(GTK_WINDOW(window_), "Engawa");

    g_signal_connect(window_, "delete-event", G_CALLBACK(&Window::onDelete), this);
    g_signal_connect(window_, "destroy", G_CALLBACK(&Window::onDestroy), this);
    g_signal_connect(window_, "configure-event", G_CALLBACK(&Window::onConfigure), this);
    g_signal_connect(window_, "focus-in-event", G_CALLBACK(&Window::onFocusIn), this);
    g_signal_connect(window_, "focus-out-event", G_CALLBACK(&Window::onFocusOut), this);
}

void Window::setWebView(GtkWidget* webview) {
    gtk_container_add(GTK_CONTAINER(window_), webview);
    gtk_widget_show_all(window_);
}

// §4.2: intercept only if the app opted in; otherwise let the window close (which ends the loop).
gboolean Window::onDelete(GtkWidget*, GdkEvent*, gpointer self) {
    auto* w = static_cast<Window*>(self);
    if (w->onUserCloseAttempt && w->onUserCloseAttempt()) return TRUE;  // opted in: keep the window
    return FALSE;  // allow the default destroy → onDestroy quits the loop
}

void Window::onDestroy(GtkWidget*, gpointer self) {
    auto* w = static_cast<Window*>(self);
    if (!w->quit_) { w->quit_ = true; gtk_main_quit(); }
}

gboolean Window::onConfigure(GtkWidget*, GdkEventConfigure* e, gpointer self) {
    auto* w = static_cast<Window*>(self);
    if (w->suppressConfigure_) { w->suppressConfigure_ = false; w->lastW_ = e->width; w->lastH_ = e->height; return FALSE; }
    if ((e->width != w->lastW_ || e->height != w->lastH_)) {
        w->lastW_ = e->width;
        w->lastH_ = e->height;
        if (w->onUserResized) w->onUserResized(e->width, e->height);
    }
    return FALSE;
}

gboolean Window::onFocusIn(GtkWidget*, GdkEventFocus*, gpointer self) {
    auto* w = static_cast<Window*>(self);
    if (w->emitter) w->emitter->emit("window.focus", Json(nullptr));
    return FALSE;
}

gboolean Window::onFocusOut(GtkWidget*, GdkEventFocus*, gpointer self) {
    auto* w = static_cast<Window*>(self);
    if (w->emitter) w->emitter->emit("window.blur", Json(nullptr));
    return FALSE;
}

void Window::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(actionMu_);
        actions_.push_back(std::move(fn));
    }
    g_idle_add(&Window::onIdle, this);  // thread-safe: schedules a drain on the main loop
}

gboolean Window::onIdle(gpointer self) {
    static_cast<Window*>(self)->drainActions();
    return G_SOURCE_REMOVE;  // one drain per scheduled idle
}

void Window::drainActions() {
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lk(actionMu_);
        batch.swap(actions_);
    }
    for (auto& fn : batch) {
        try { fn(); } catch (...) { /* an action must not kill the loop */ }
    }
}

void Window::runMessageLoop() { gtk_main(); }

void Window::applyClientSize(int w, int h) {
    suppressConfigure_ = true;
    lastW_ = w;
    lastH_ = h;
    gtk_window_resize(GTK_WINDOW(window_), w, h);
}

void Window::applyTitle(const std::string& title) { gtk_window_set_title(GTK_WINDOW(window_), title.c_str()); }

void Window::applyResizable(bool resizable) { gtk_window_set_resizable(GTK_WINDOW(window_), resizable); }

void Window::doMinimize() { gtk_window_iconify(GTK_WINDOW(window_)); }
void Window::doMaximize() { gtk_window_maximize(GTK_WINDOW(window_)); }

void Window::closeWindow() {
    if (!quit_) { quit_ = true; gtk_main_quit(); }
}
