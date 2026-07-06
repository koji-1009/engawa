#pragma once
// The real desktop-notification path, factored out of NotificationAdapter. It posts to
// org.freedesktop.Notifications via GIO's GDBus (no extra dependency) — the standard Notify method on
// the session bus, the same service libnotify uses.
#include <string>

namespace native {

// Post a desktop notification. Throws std::runtime_error (with the D-Bus error) on failure.
void showToast(const std::string& title, const std::string& body);

}  // namespace native
