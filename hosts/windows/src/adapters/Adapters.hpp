#pragma once
// Factory declarations for the built-in namespace adapters (§4) and the two reference/contract-coupled
// adapters. Each is defined in its own translation unit; main composes them into the Dispatcher —
// static composition at app build time (§3, "no dynamic loading"). The sqlite/update factories are
// defined under adapters/*/hosts/windows/ (§3 layout) and compiled into this one exe.
#include <functional>
#include <memory>
#include <string>

#include "Adapter.hpp"
#include "HostOptions.hpp"
#include "IoChannel.hpp"
#include "UpdateHost.hpp"
#include "Window.hpp"

std::unique_ptr<IAdapter> makeEchoAdapter();
std::unique_ptr<IAdapter> makeWindowAdapter(Window& window, IEventEmitter* emitter, const HostOptions& opts);
std::unique_ptr<IAdapter> makeDialogAdapter(Window& window, const HostOptions& opts);
std::unique_ptr<IAdapter> makeFsAdapter(IoChannel& io);
std::unique_ptr<IAdapter> makePathAdapter(const HostOptions& opts);
std::unique_ptr<IAdapter> makeClipboardAdapter();
std::unique_ptr<IAdapter> makeShellOpenAdapter(const HostOptions& opts);
std::unique_ptr<IAdapter> makeNotificationAdapter(const HostOptions& opts);
std::unique_ptr<IAdapter> makeProcessAdapter(IEventEmitter* emitter, const HostOptions& opts);
std::unique_ptr<IAdapter> makeAppAdapter(std::string appVersion, std::function<std::string()> engineVersion,
                                         const HostOptions& opts, std::function<void()> onQuit);

// Reference adapter (extractable) + contract-coupled adapter (§3), defined under adapters/.
std::unique_ptr<IAdapter> makeSqliteAdapter();
std::unique_ptr<IAdapter> makeUpdateAdapter(UpdateHost& host, const HostOptions& opts);
