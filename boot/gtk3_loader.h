#pragma once
// -----------------------------------------------------------------------------
//  boot/gtk3_loader.h — GTK3 by dlopen: no headers, no link dependency
// -----------------------------------------------------------------------------
//  Why: the bootstrapper must run on operator boxes that may lack gtk3-devel
//  (setup.sh installs the runtime, but nothing should hard-link a toolkit the
//  machine might not have at build time, and this sandbox cannot apt-install
//  anything). So we resolve exactly the 29 libgtk-3/libglib symbols we use at
//  runtime; if libgtk-3.so.0 is absent the --console mode still works and the
//  failure message is one line instead of a loader error. It also lets the
//  sandbox smoke-test the whole UI flow against a stub .so — verified here.
//
//  The API surface is a plain struct of typed function pointers (enums from
//  GTK3 are stable ABI constants: TOPLEVEL=0, VERTICAL=1, WIN_POS_CENTER=1,
//  POLICY_AUTOMATIC=1). Only the slice of GTK the bootstrap screen needs —
//  the roadmap (docs/BOOTSTRAPPER.md) grows this file, not the app's.
// -----------------------------------------------------------------------------

namespace gtk3 {

struct Api {
    // init / windows / boxes
    int  (*init_check)(int*, char***);
    void* (*window_new)(int);                       // GtkWindowType
    void (*window_set_title)(void*, const char*);
    void (*window_set_default_size)(void*, int, int);
    void (*window_set_position)(void*, int);        // GtkWindowPosition
    void (*container_add)(void*, void*);
    void* (*box_new)(int, int);                     // GtkOrientation, spacing
    void (*box_pack_start)(void*, void*, int, int, int);
    void (*widget_show_all)(void*);
    void (*widget_destroy)(void*);
    void (*widget_set_sensitive)(void*, int);
    // widgets
    void* (*label_new)(const char*);
    void (*label_set_text)(void*, const char*);
    void* (*button_new_with_label)(const char*);
    void* (*text_view_new)(void);
    void* (*text_view_get_buffer)(void*);
    void (*text_buffer_set_text)(void*, const char*, int);
    void* (*scrolled_window_new)(void*, void*);
    void (*scrolled_window_set_policy)(void*, int, int);
    void* (*spinner_new)(void);
    void (*spinner_start)(void*);
    void (*spinner_stop)(void*);
    // glib
    void* (*object_ref)(void*);
    void (*object_unref)(void*);
    unsigned (*timeout_add)(unsigned, int (*)(void*), void*);
    int (*source_remove)(unsigned);
    unsigned long (*signal_connect_data)(void*, const char*, void (*)(void*, void*),
                                         void*, void*, int);
    // loop
    void (*main)(void);
    void (*main_quit)(void);
};

// GTK3 ABI enums (stable values; re-declared so no GTK header is needed).
enum : int { WIN_TOPLEVEL = 0, ORIENT_VERTICAL = 1, WIN_POS_CENTER = 1,
             POLICY_AUTOMATIC = 1 };

// Resolve libgtk-3 + libglib. path_override (optional) is tried first — used
// by the smoke test to inject a stub .so. False sets LastError().
bool Load(const char* path_override = nullptr);
const char* LastError();

// Valid after Load(). Null before.
Api* Get();
bool Available();

} // namespace gtk3
