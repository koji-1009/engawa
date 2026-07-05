#pragma once
// The real system-toast path (Windows.UI.Notifications), factored out of NotificationAdapter so both
// the adapter and the native smoke test (hosts/windows/test/) exercise the SAME code — the WinRT XML
// build, the AppUserModelID registration, and the notifier activation.
#include <string>

namespace native {

// Post a system toast. Throws std::runtime_error (with the WinRT message) on failure.
void showToast(const std::string& title, const std::string& body);

}  // namespace native
