#include "Window.hpp"

#include <stdexcept>

#include "Utf.hpp"

namespace {
constexpr UINT WM_APP_RUN = WM_APP + 1;  // "run queued UI-thread actions"
constexpr wchar_t kClassName[] = L"EngawaHostWindow";
constexpr int kOffscreen = -32000;       // hidden window position for the suite / autotest gate
}  // namespace

Window::Window(bool hidden) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Window::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc) == 0) throw std::runtime_error("RegisterClassExW failed");
        registered = true;
    }

    hwnd_ = CreateWindowExW(
        0, kClassName, L"Engawa", WS_OVERLAPPEDWINDOW,
        hidden ? kOffscreen : CW_USEDEFAULT, hidden ? kOffscreen : CW_USEDEFAULT,
        1024, 720, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) throw std::runtime_error("CreateWindowExW failed");
    if (!hidden) ShowWindow(hwnd_, SW_SHOWNORMAL);
}

LRESULT CALLBACK Window::wndProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    LRESULT result = 0;
    if (self && self->handle(msg, wParam, lParam, result)) return result;
    return DefWindowProcW(h, msg, wParam, lParam);
}

bool Window::handle(UINT msg, WPARAM, LPARAM, LRESULT& result) {
    result = 0;
    switch (msg) {
        case WM_SIZE: {
            RECT r{};
            if (controller_ && GetClientRect(hwnd_, &r))
                controller_->put_Bounds(r);
            if (!programmaticResize_ && onUserResized && GetClientRect(hwnd_, &r))
                onUserResized(r.right - r.left, r.bottom - r.top);
            return true;
        }
        case WM_SETFOCUS:
            if (emitter) emitter->emit("window.focus", Json(nullptr));
            return false;  // let DefWindowProc run too
        case WM_KILLFOCUS:
            if (emitter) emitter->emit("window.blur", Json(nullptr));
            return false;
        case WM_CLOSE:
            // §4.2: intercept only if the app opted in; otherwise destroy the window.
            if (onUserCloseAttempt && onUserCloseAttempt()) return true;
            DestroyWindow(hwnd_);
            return true;
        case WM_APP_RUN:
            drainActions();
            return true;
        case WM_DESTROY:
            PostQuitMessage(0);
            return true;
    }
    return false;
}

void Window::post(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(actionMu_);
        actions_.push_back(std::move(fn));
    }
    PostMessageW(hwnd_, WM_APP_RUN, 0, 0);
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

void Window::runMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Window::applyClientSize(int w, int h) {
    programmaticResize_ = true;
    RECT rect{0, 0, w, h};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    programmaticResize_ = false;
}

void Window::applyTitle(const std::string& title) { SetWindowTextW(hwnd_, ToWide(title).c_str()); }

void Window::applyResizable(bool resizable) {
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    LONG_PTR bits = WS_THICKFRAME | WS_MAXIMIZEBOX;
    style = resizable ? (style | bits) : (style & ~bits);
    SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::doMinimize() { ShowWindow(hwnd_, SW_MINIMIZE); }
void Window::doMaximize() { ShowWindow(hwnd_, SW_MAXIMIZE); }
void Window::closeWindow() { DestroyWindow(hwnd_); }
