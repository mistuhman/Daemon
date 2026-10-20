/* fake_gtk3.c — a stub libgtk-3.so.0 for bootstrapper smoke tests (sandbox /
 * CI: no GTK, no X11, no Sober). Every call is recorded; gtk_main runs the
 * registered g_timeout_add callbacks a few times, fires every "clicked"
 * handler once (Load then Unload, registration order), then quits.
 *
 * build:  cc -shared -fPIC -o fake/libgtk-3.so.0 boot/test/fake_gtk3.c
 * use:    LD_LIBRARY_PATH=fake ./roblox-external-boot
 *
 * The bootstrapper resolves GTK by dlopen (boot/gtk3_loader.cpp), so this
 * stub drops in without touching the binary. Contract it must satisfy: the
 * exact 29 symbols in gtk3_loader.cpp's table. That list is the smoke's
 * checklist — if the loader grows a symbol, it is listed here too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int  fake_init_check(int* argc, char*** argv) { (void)argc; (void)argv; return 1; }
static void* fake_window_new(int t)                 { (void)t; static int w; return &w; }
static void fake_set_title(void* w, const char* s)  { (void)w; printf("[gtk] title=%s\n", s); }
static void fake_win_size(void* w, int a, int b)    { (void)w; (void)a; (void)b; }
static void fake_win_pos(void* w, int p)            { (void)w; (void)p; }
static void fake_container_add(void* a, void* b)    { (void)a; (void)b; }
static void* fake_box_new(int o, int s)             { (void)o; (void)s; static int b; return &b; }
static void fake_box_pack(void* a, void* b, int c, int d, int e)
    { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void fake_show_all(void* w)                  { (void)w; }
static void fake_destroy(void* w)                   { (void)w; }
static void fake_sensitive(void* w, int s)          { (void)w; (void)s; }

static void* fake_label_new(const char* s)          { printf("[gtk] label_new=%s\n", s); static int l; return &l; }
static void fake_label_set(void* l, const char* s)  { (void)l; printf("[gtk] label=%s\n", s); }
static void* fake_button_new(const char* s)         { printf("[gtk] button=%s\n", s); static int b; return &b; }

static void* fake_text_view(void)                   { static int v; return &v; }
static void* fake_text_buffer(void* v)              { (void)v; static int b; return &b; }
static void fake_buffer_set(void* b, const char* t, int n)
    { (void)b; const char* p = t + (n > 200 ? n - 200 : 0);
      printf("[gtk] log(%d)=%s\n", n, p); }
static void* fake_scroll(void* a, void* b)          { (void)a; (void)b; static int s; return &s; }
static void fake_scroll_policy(void* s, int x, int y) { (void)s; (void)x; (void)y; }
static void* fake_spinner(void)                     { static int s; return &s; }
static void fake_spin_start(void* s)                { (void)s; }
static void fake_spin_stop(void* s)                 { (void)s; }

static void* fake_ref(void* o)                      { return o; }
static void fake_unref(void* o)                     { (void)o; }

static struct { int (*fn)(void*); void* data; } g_timers[8];
static int g_ntimers = 0;

static unsigned fake_timeout_add(unsigned ms, int (*fn)(void*), void* data)
{
    (void)ms;
    if (g_ntimers < 8) { g_timers[g_ntimers].fn = fn; g_timers[g_ntimers].data = data; }
    return (unsigned)++g_ntimers;
}
static int fake_source_remove(unsigned id)          { (void)id; return 1; }

static struct { void* obj; void (*cb)(void*, void*); void* data; } g_clicks[8];
static int g_nclicks = 0;

static unsigned long fake_signal_connect(void* obj, const char* name,
                                         void (*cb)(void*, void*),
                                         void* data, void* dd, int flags)
{
    (void)dd; (void)flags;
    if (name && strcmp(name, "clicked") == 0 && g_nclicks < 8) {
        g_clicks[g_nclicks].obj = obj;
        g_clicks[g_nclicks].cb = cb;
        g_clicks[g_nclicks].data = data;
        g_nclicks++;
    }
    return 1;
}

static int g_quit = 0;
static void fake_main(void)
{
    printf("[gtk] main() begin — running timers, then one click each\n");
    for (int round = 0; round < 3 && !g_quit; ++round) {
        usleep(50 * 1000);
        for (int i = 0; i < g_ntimers && i < 8 && !g_quit; ++i)
            if (g_timers[i].fn) g_timers[i].fn(g_timers[i].data);
    }
    for (int i = 0; i < g_nclicks && !g_quit; ++i) {
        printf("[gtk] firing clicked #%d\n", i);
        if (g_clicks[i].cb) g_clicks[i].cb(g_clicks[i].obj, g_clicks[i].data);
    }
    for (int round = 0; round < 2 && !g_quit; ++round) {
        usleep(50 * 1000);
        for (int i = 0; i < g_ntimers && i < 8 && !g_quit; ++i)
            if (g_timers[i].fn) g_timers[i].fn(g_timers[i].data);
    }
    printf("[gtk] main() end\n");
}
static void fake_main_quit(void) { g_quit = 1; }

/* The 29 symbols, names exactly as gtk3_loader.cpp resolves them. */
void* gtk_init_check(int* a, char*** b)                { return (void*)(long)fake_init_check(a, b); }
void* gtk_window_new(int t)                            { return fake_window_new(t); }
void  gtk_window_set_title(void* w, const char* s)     { fake_set_title(w, s); }
void  gtk_window_set_default_size(void* w, int a, int b) { fake_win_size(w, a, b); }
void  gtk_window_set_position(void* w, int p)          { fake_win_pos(w, p); }
void  gtk_container_add(void* a, void* b)              { fake_container_add(a, b); }
void* gtk_box_new(int o, int s)                        { return fake_box_new(o, s); }
void  gtk_box_pack_start(void* a, void* b, int c, int d, int e) { fake_box_pack(a, b, c, d, e); }
void  gtk_widget_show_all(void* w)                     { fake_show_all(w); }
void  gtk_widget_destroy(void* w)                      { fake_destroy(w); }
void  gtk_widget_set_sensitive(void* w, int s)         { fake_sensitive(w, s); }
void* gtk_label_new(const char* s)                     { return fake_label_new(s); }
void  gtk_label_set_text(void* l, const char* s)       { fake_label_set(l, s); }
void* gtk_button_new_with_label(const char* s)         { return fake_button_new(s); }
void* gtk_text_view_new(void)                          { return fake_text_view(); }
void* gtk_text_view_get_buffer(void* v)                { return fake_text_buffer(v); }
void  gtk_text_buffer_set_text(void* b, const char* t, int n) { fake_buffer_set(b, t, n); }
void* gtk_scrolled_window_new(void* a, void* b)        { return fake_scroll(a, b); }
void  gtk_scrolled_window_set_policy(void* s, int x, int y) { fake_scroll_policy(s, x, y); }
void* gtk_spinner_new(void)                            { return fake_spinner(); }
void  gtk_spinner_start(void* s)                       { fake_spin_start(s); }
void  gtk_spinner_stop(void* s)                        { fake_spin_stop(s); }
void* g_object_ref(void* o)                            { return fake_ref(o); }
void  g_object_unref(void* o)                          { fake_unref(o); }
unsigned g_timeout_add(unsigned ms, int (*fn)(void*), void* d) { return fake_timeout_add(ms, fn, d); }
int  g_source_remove(unsigned id)                      { return fake_source_remove(id); }
unsigned long g_signal_connect_data(void* o, const char* n, void (*cb)(void*, void*),
                                    void* d, void* dd, int f)
    { return fake_signal_connect(o, n, cb, d, dd, f); }
void  gtk_main(void)                                   { fake_main(); }
void  gtk_main_quit(void)                              { fake_main_quit(); }
