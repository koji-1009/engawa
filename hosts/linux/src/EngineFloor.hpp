#pragma once
// Engine floor check (contract §9). Below the minimum → no partial boot; the host reports the
// rejection and shows the spec'd error screen. ENGAWA_FAKE_ENGINE_VERSION substitutes the detected
// version so the suite can exercise rejection on any machine — it feeds only this comparison and
// MUST NOT bypass §7.1 (it does not).
#include <string>

namespace EngineFloor {

// The WebKitGTK floor for the Linux reference host, defined normatively in spec/contract.md §9
// (WebKitGTK 2.28.0 — the webkit2gtk-4.1/GTK3 API baseline; a Chromium-style floor would spuriously
// reject the 2.x WebKitGTK versions). The conformance driver supplies WebKit-shaped straddle samples
// (below 2.28, above it), so §9 exercises correctly on any machine.
inline constexpr const char* Required = "2.28.0";

// Compare dotted numeric version tuples component-wise; missing/non-numeric components read as 0, so
// a malformed string degrades to "very old" rather than crashing boot.
int compare(const std::string& a, const std::string& b);
bool isBelowFloor(const std::string& detected);

}  // namespace EngineFloor
