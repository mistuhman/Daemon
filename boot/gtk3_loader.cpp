// -----------------------------------------------------------------------------
//  boot/gtk3_loader.cpp — symbol resolution (see header)
// -----------------------------------------------------------------------------
#include "gtk3_loader.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

namespace gtk3 {
namespace {

void* g_lib = nullptr;
Api g_api{};
char g_err[256] = "";

struct Entry { const char* name; void** slot; };

bool Resolve(void* lib)
{
    const Entry table[] = {
        { "gtk_init_check",           (void**)&g_api.init_check },
        { "gtk_window_new",           (void**)&g_api.window_new },
        { "gtk_window_set_title",     (void**)&g_api.window_set_title },
        { "gtk_window_set_default_size", (void**)&g_api.window_set_default_size },
        { "gtk_window_set_position",  (void**)&g_api.window_set_position },
        { "gtk_container_add",        (void**)&g_api.container_add },
        { "gtk_box_new",              (void**)&g_api.box_new },
        { "gtk_box_pack_start",       (void**)&g_api.box_pack_start },
        { "gtk_widget_show_all",      (void**)&g_api.widget_show_all },
        { "gtk_widget_destroy",       (void**)&g_api.widget_destroy },
        { "gtk_widget_set_sensitive", (void**)&g_api.widget_set_sensitive },
        { "gtk_label_new",            (void**)&g_api.label_new },
        { "gtk_label_set_text",       (void**)&g_api.label_set_text },
        { "gtk_button_new_with_label",(void**)&g_api.button_new_with_label },
        { "gtk_text_view_new",        (void**)&g_api.text_view_new },
        { "gtk_text_view_get_buffer", (void**)&g_api.text_view_get_buffer },
        { "gtk_text_buffer_set_text", (void**)&g_api.text_buffer_set_text },
        { "gtk_scrolled_window_new",  (void**)&g_api.scrolled_window_new },
        { "gtk_scrolled_window_set_policy", (void**)&g_api.scrolled_window_set_policy },
        { "gtk_spinner_new",          (void**)&g_api.spinner_new },
        { "gtk_spinner_start",        (void**)&g_api.spinner_start },
        { "gtk_spinner_stop",         (void**)&g_api.spinner_stop },
        { "g_object_ref",             (void**)&g_api.object_ref },
        { "g_object_unref",           (void**)&g_api.object_unref },
        { "g_timeout_add",            (void**)&g_api.timeout_add },
        { "g_source_remove",          (void**)&g_api.source_remove },
        { "g_signal_connect_data",    (void**)&g_api.signal_connect_data },
        { "gtk_main",                 (void**)&g_api.main },
        { "gtk_main_quit",            (void**)&g_api.main_quit },
    };
    for (const Entry& e : table) {
        *e.slot = dlsym(lib, e.name);
        if (!*e.slot) {
            snprintf(g_err, sizeof(g_err), "missing symbol %s", e.name);
            return false;
        }
    }
    return true;
}

} // namespace

bool Load(const char* path_override)
{
    if (g_lib) return true;
    const char* candidates[] = {
        path_override, "libgtk-3.so.0", "libgtk-3.so", nullptr
    };
    for (int i = 0; candidates[i] || i == 0; ++i) {
        const char* c = candidates[i];
        if (!c || !c[0]) continue;
        g_lib = dlopen(c, RTLD_NOW | RTLD_GLOBAL);
        if (g_lib) break;
    }
    if (!g_lib) {
        snprintf(g_err, sizeof(g_err), "libgtk-3.so.0 not found (%s)", dlerror());
        return false;
    }
    if (!Resolve(g_lib)) {
        dlclose(g_lib);
        g_lib = nullptr;
        return false;
    }
    return true;
}

const char* LastError() { return g_err; }
Api* Get() { return g_lib ? &g_api : nullptr; }
bool Available() { return g_lib != nullptr; }

} // namespace gtk3
