#pragma once
// Engine floor check (contract §9). Below the minimum → no partial boot; the host reports the
// rejection and shows the spec'd error screen. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected
// version so the suite can exercise rejection on any machine — it feeds only this comparison and
// MUST NOT bypass §7.1 (it does not).
#include <string>

namespace EngineFloor {

// The WebView2/Chromium floor for the Windows reference host (spec/contract.md §9). The Evergreen
// runtime auto-updates, so this only guards an ancient pinned runtime; a Chromium major of 90 is a
// conservative minimum for the Win10 1809 baseline.
inline constexpr const char* Required = "90.0.0.0";

// Compare dotted numeric version tuples component-wise; missing/non-numeric components read as 0, so
// a malformed string degrades to "very old" rather than crashing boot.
int compare(const std::string& a, const std::string& b);
bool isBelowFloor(const std::string& detected);

}  // namespace EngineFloor
