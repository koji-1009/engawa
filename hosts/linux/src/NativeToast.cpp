#include "NativeToast.hpp"

#include <gio/gio.h>

#include <stdexcept>

namespace native {

// Post a desktop notification over the standard org.freedesktop.Notifications D-Bus service (the
// same path libnotify uses, but with no extra dependency — GIO ships with GLib). Best effort: throws
// std::runtime_error if the session bus or a notification daemon is absent, so the adapter can map it
// to EIO and the app can fall back.
void showToast(const std::string& title, const std::string& body) {
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!bus) {
        std::string m = err ? err->message : "no session bus";
        if (err) g_error_free(err);
        throw std::runtime_error(m);
    }

    // Notify(app_name s, replaces_id u, app_icon s, summary s, body s, actions as, hints a{sv}, timeout i)
    GVariantBuilder actions, hints;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    GVariant* params = g_variant_new("(susssasa{sv}i)", "engawa", 0u, "", title.c_str(), body.c_str(),
                                     &actions, &hints, -1);

    GVariant* result = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify", params, nullptr, G_DBUS_CALL_FLAGS_NONE, 3000,
        nullptr, &err);

    if (!result) {
        std::string m = err ? err->message : "Notify call failed";
        if (err) g_error_free(err);
        g_object_unref(bus);
        throw std::runtime_error(m);
    }
    g_variant_unref(result);
    g_object_unref(bus);
}

}  // namespace native
