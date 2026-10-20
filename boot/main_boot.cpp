// -----------------------------------------------------------------------------
//  boot/main_boot.cpp — the bootstrapper window (dx9ware shape)
// -----------------------------------------------------------------------------
//  One screen, deliberately: watch for Sober -> ready -> Load. Load launches
//  the overlay --locked (geometry follows Sober, never a free-floating
//  desktop overlay) and the bootstrapper owns its lifecycle: closing Sober or
//  the bootstrap window stops the overlay. The screen is only the first face
//  of a manager (docs/BOOTSTRAPPER.md: update, doctor, profiles grow behind
//  it) so every behavior here calls bootcore; the GTK layer is glue.
//
//  Modes:
//    (default)             GTK window (needs libgtk-3.so.0 at runtime)
//    --console             terminal status loop, no GTK needed
//    --auto-load           load the overlay the moment readiness is reached
//    --exit-after N        exit after N seconds (0 = run forever; smoke/tests)
//    --check               print one readiness scan and exit 0 (CI probe)
//    --force-load          skip the readiness gate (smoke tests only)
// -----------------------------------------------------------------------------
#include "bootstrap_core.h"
#include "version.h"
#include "gtk3_loader.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <time.h>
#include <unistd.h>

namespace {

struct App {
    void* window = nullptr;
    void* status_label = nullptr;
    void* detail_label = nullptr;
    void* spinner = nullptr;
    void* load_btn = nullptr;
    void* unload_btn = nullptr;
    void* log_buffer = nullptr;
    std::string log;
    std::string last_status;
    bool auto_load = false;
    bool auto_loaded = false;
    bool force_load = false;   // smoke tests: skip the readiness gate
    time_t started = 0;
    int exit_after = 0;
};

App g_app;

void Blog(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char stamp[32];
    const time_t t = time(nullptr);
    struct tm tmv{};
    localtime_r(&t, &tmv);
    strftime(stamp, sizeof(stamp), "%H:%M:%S ", &tmv);
    g_app.log += stamp;
    g_app.log += buf;
    g_app.log += "\n";
    if (g_app.log.size() > 8192) g_app.log.erase(0, g_app.log.size() - 6144);
    if (g_app.log_buffer) {
        gtk3::Get()->text_buffer_set_text(g_app.log_buffer, g_app.log.c_str(),
                                          (int)g_app.log.size());
    }
    if (!gtk3::Available()) printf("%s%s\n", stamp, buf);
}

int Tick(void*)
{
    const bootcore::Status s = bootcore::Poll();

    if (gtk3::Available()) {
        gtk3::Api* g = gtk3::Get();
        g->label_set_text(g_app.status_label, s.line);
        g->label_set_text(g_app.detail_label, s.detail);
        g->widget_set_sensitive(g_app.load_btn, s.ready && !s.overlay_running ? 1 : 0);
        g->widget_set_sensitive(g_app.unload_btn, s.overlay_running ? 1 : 0);
        if (s.ready && !s.overlay_running) g->spinner_start(g_app.spinner);
        else g->spinner_stop(g_app.spinner);
    }

    if (s.line != g_app.last_status) {
        Blog("status: %s", s.line);
        if (s.detail[0]) Blog("        %s", s.detail);
        g_app.last_status = s.line;
    }

    if (g_app.auto_load && !g_app.auto_loaded &&
        (s.ready || g_app.force_load) && !s.overlay_running) {
        g_app.auto_loaded = true;
        char err[256];
        if (bootcore::StartOverlay(err, sizeof(err))) {
            Blog("auto-load: overlay launched (--locked)");
            Blog("overlay log: %s", bootcore::OverlayLogPathForUi().c_str());
        } else {
            Blog("auto-load failed: %s", err);
        }
    }

    if (g_app.exit_after > 0 &&
        (time(nullptr) - g_app.started) >= g_app.exit_after) {
        if (s.overlay_running) bootcore::StopOverlay();
        if (gtk3::Available()) gtk3::Get()->main_quit();
        return 0;
    }
    return 1; // keep the timer alive
}

void OnLoad(void*, void*)
{
    char err[256];
    if (bootcore::StartOverlay(err, sizeof(err))) {
        Blog("Load: overlay launched (--locked — hidden unless Sober's window is up)");
        Blog("overlay log: %s", bootcore::OverlayLogPathForUi().c_str());
    } else
        Blog("Load failed: %s", err);
}

void OnUnload(void*, void*)
{
    if (bootcore::StopOverlay()) Blog("Unload: SIGTERM sent to the overlay (writes restored)");
    else Blog("Unload: overlay is not running");
}

void OnDestroy(void*, void*) { gtk3::Get()->main_quit(); }

int RunGtk(int argc, char** argv)
{
    if (!gtk3::Load(nullptr)) {
        printf("daemon-boot: %s\n"
               "  install GTK3 (e.g. xbps-install gtk+3) or use --console\n",
               gtk3::LastError());
        return 3;
    }
    gtk3::Api* g = gtk3::Get();
    if (!g->init_check(&argc, &argv)) {
        printf("daemon-boot: no graphical display (set DISPLAY or use --console)\n");
        return 3;
    }

    g_app.window = g->window_new(gtk3::WIN_TOPLEVEL);
    g->window_set_title(g_app.window, DAEMON_DISPLAY_NAME " bootstrapper");
    g->window_set_default_size(g_app.window, 560, 420);
    g->window_set_position(g_app.window, gtk3::WIN_POS_CENTER);

    void* vbox = g->box_new(gtk3::ORIENT_VERTICAL, 8);
    g->container_add(g_app.window, vbox);

    void* title = g->label_new(DAEMON_DISPLAY_NAME " — Linux / Sober");
    g->box_pack_start(vbox, title, 0, 0, 4);

    g_app.spinner = g->spinner_new();
    g->box_pack_start(vbox, g_app.spinner, 0, 0, 0);

    g_app.status_label = g->label_new("starting...");
    g_app.detail_label = g->label_new("");
    g->box_pack_start(vbox, g_app.status_label, 0, 0, 0);
    g->box_pack_start(vbox, g_app.detail_label, 0, 0, 0);

    g_app.load_btn = g->button_new_with_label("Load");
    g_app.unload_btn = g->button_new_with_label("Unload");
    g->widget_set_sensitive(g_app.load_btn, 0);
    g->widget_set_sensitive(g_app.unload_btn, 0);
    g->box_pack_start(vbox, g_app.load_btn, 0, 0, 0);
    g->box_pack_start(vbox, g_app.unload_btn, 0, 0, 0);

    void* scroll = g->scrolled_window_new(nullptr, nullptr);
    g->scrolled_window_set_policy(scroll, gtk3::POLICY_AUTOMATIC, gtk3::POLICY_AUTOMATIC);
    void* view = g->text_view_new();
    g_app.log_buffer = g->text_view_get_buffer(view);
    g->container_add(scroll, view);
    g->box_pack_start(vbox, scroll, 1, 1, 0);

    g->signal_connect_data(g_app.load_btn, "clicked", OnLoad, nullptr, nullptr, 0);
    g->signal_connect_data(g_app.unload_btn, "clicked", OnUnload, nullptr, nullptr, 0);
    g->signal_connect_data(g_app.window, "destroy", OnDestroy, nullptr, nullptr, 0);

    g->widget_show_all(g_app.window);
    Blog(DAEMON_DISPLAY_NAME ": bootstrapper up (streamproof + --locked overlay contract ready)");
    Blog("overlay log will be: %s", bootcore::OverlayLogPathForUi().c_str());
    Tick(nullptr);
    g->timeout_add(1000, Tick, nullptr);
    g->main();
    if (bootcore::Poll().overlay_running) bootcore::StopOverlay();
    return 0;
}

int RunConsole()
{
    Blog("bootstrapper console mode — watching for Sober");
    for (;;) {
        Tick(nullptr);
        if (g_app.exit_after > 0 && (time(nullptr) - g_app.started) >= g_app.exit_after) {
            if (bootcore::Poll().overlay_running) bootcore::StopOverlay();
            return 0;
        }
        sleep(1);
    }
}

} // namespace

int main(int argc, char** argv)
{
    g_app.started = time(nullptr);
    bool console = false, check = false, force_load = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--console")) console = true;
        else if (!strcmp(argv[i], "--auto-load")) g_app.auto_load = true;
        else if (!strcmp(argv[i], "--check")) check = true;
        else if (!strcmp(argv[i], "--force-load")) force_load = true;
        else if (!strcmp(argv[i], "--exit-after") && i + 1 < argc) g_app.exit_after = atoi(argv[++i]);
    }
    g_app.force_load = force_load;

    if (check) {
        const bootcore::Status s = bootcore::Poll();
        printf("sober=%d pid=%u libroblox=%d yama=%d display=%d overlay=%d\n",
               (int)s.sober, s.sober_pid, (int)s.libroblox, s.yama,
               (int)s.display, (int)s.overlay_file);
        printf("status: %s\n", s.line);
        return 0;
    }
    if (console) return RunConsole();
    return RunGtk(argc, argv);
}
