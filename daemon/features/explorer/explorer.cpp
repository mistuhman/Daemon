#include "explorer.h"
// -----------------------------------------------------------------------------
//  features/explorer/explorer.cpp  --  the DataModel object browser (dex)
// -----------------------------------------------------------------------------
//  Two threads, one snapshot:
//
//    worker  walks the instance tree breadth-first from the DataModel, records
//            parent/child ranges (BFS order makes every node's children a
//            CONTIGUOUS range, so a row knows its kids with two ints) and
//            publishes it. A search for a subclass of the walk scans names in
//            the same worker.
//
//    render  reads the snapshot, builds the visible row list from the expansion
//            set, and reads name/class only for rows ImGui actually draws.
//
//  The only shared mutable state is `std::shared_ptr<const Snap>` behind a mutex,
//  and the mutex is held for the pointer copy, not for the draw.
// -----------------------------------------------------------------------------
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "imgui.h"
#include "game.h"
#include "globals.h"
#include "memory.h"
#include "platform/platform.h"

namespace features {
namespace explorer {

namespace {

    // Caps chosen against a real map: 20k nodes covers every normal place, the
    // depth cap stops a cyclic Parent chain (which a corrupt read can produce)
    // from walking forever, and the time budget keeps the worker polite.
    constexpr int    kMaxNodes   = 20000;
    constexpr int    kMaxDepth   = 18;
    constexpr int    kMaxRows    = 6000;    // rows built per frame
    constexpr int    kMaxMatches = 500;
    constexpr uint64_t kBudgetMs = 400;

    struct Node {
        uintptr_t addr = 0;
        int parent = -1;
        int depth = 0;
        int first = 0;      // children are nodes[first..last)
        int last = 0;
    };

    struct Snap {
        std::vector<Node> nodes;          // [0] is the DataModel itself
        std::vector<uintptr_t> matches;   // search results (flat addresses)
        std::string query;                // what `matches` answers
        bool truncated = false;
        uint64_t built_ms = 0;
    };

    std::mutex g_snap_mutex;
    std::shared_ptr<const Snap> g_snap;
    std::shared_ptr<const Snap> g_search;

    std::atomic<bool> g_want{ false };
    std::atomic<bool> g_want_search{ false };
    std::atomic<bool> g_running{ false };
    std::atomic<bool> g_started{ false };
    std::atomic<uint64_t> g_last_build_ms{ 0 };

    std::mutex g_query_mutex;
    std::string g_query;

    void publish(std::shared_ptr<const Snap> snap, bool is_search) {
        std::lock_guard<std::mutex> lock(g_snap_mutex);
        if (is_search) g_search = std::move(snap);
        else           g_snap = std::move(snap);
    }

    std::shared_ptr<const Snap> snapshot(bool is_search) {
        std::lock_guard<std::mutex> lock(g_snap_mutex);
        return is_search ? g_search : g_snap;
    }

    bool contains_ci(const std::string& hay, const std::string& needle) {
        if (needle.empty()) return true;
        if (needle.size() > hay.size()) return false;
        for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
            size_t j = 0;
            for (; j < needle.size(); ++j) {
                const char a = hay[i + j], b = needle[j];
                const char la = (a >= 'A' && a <= 'Z') ? (char)(a - 'A' + 'a') : a;
                const char lb = (b >= 'A' && b <= 'Z') ? (char)(b - 'A' + 'a') : b;
                if (la != lb) break;
            }
            if (j == needle.size()) return true;
        }
        return false;
    }

    void build_tree() {
        auto snap = std::make_shared<Snap>();
        snap->built_ms = platform::NowMs();
        g_last_build_ms.store(snap->built_ms, std::memory_order_relaxed);

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) {
            publish(std::move(snap), false);
            return;
        }

        snap->nodes.push_back(Node{ dm.address, -1, 0, 1, 1 });
        std::vector<int> queue;
        queue.push_back(0);

        const uint64_t deadline = snap->built_ms + kBudgetMs;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            const int idx = queue[qi];
            if ((qi & 63u) == 0 && platform::NowMs() > deadline) {
                snap->truncated = true;
                break;
            }
            const int depth = snap->nodes[(size_t)idx].depth;
            const int first = (int)snap->nodes.size();
            if (depth < kMaxDepth) {
                instance in{ snap->nodes[(size_t)idx].addr };
                for (const instance& c : in.get_children()) {
                    if (!c.is_valid()) continue;
                    if ((int)snap->nodes.size() >= kMaxNodes) {
                        snap->truncated = true;
                        break;
                    }
                    Node n;
                    n.addr = c.address;
                    n.parent = idx;
                    n.depth = depth + 1;
                    snap->nodes.push_back(n);
                    queue.push_back((int)snap->nodes.size() - 1);
                }
            }
            snap->nodes[(size_t)idx].first = first;
            snap->nodes[(size_t)idx].last = (int)snap->nodes.size();
            if ((int)snap->nodes.size() >= kMaxNodes) {
                snap->truncated = true;
                break;
            }
        }

        publish(std::move(snap), false);
    }

    void search_snapshot() {
        std::string query;
        {
            std::lock_guard<std::mutex> lock(g_query_mutex);
            query = g_query;
        }
        auto out = std::make_shared<Snap>();
        out->query = query;
        out->built_ms = platform::NowMs();
        if (query.empty()) {
            publish(std::move(out), true);
            return;
        }

        std::shared_ptr<const Snap> base = snapshot(false);
        if (!base) {
            publish(std::move(out), true);
            return;
        }
        out->truncated = base->truncated;

        const uint64_t deadline = platform::NowMs() + kBudgetMs;
        for (size_t i = 0; i < base->nodes.size(); ++i) {
            if ((i & 31u) == 0 && platform::NowMs() > deadline) break;
            const instance in{ base->nodes[i].addr };
            const std::string cls = in.get_class_name();
            const std::string nm = in.get_name();
            if (contains_ci(cls, query) || contains_ci(nm, query)) {
                out->matches.push_back(base->nodes[i].addr);
                if ((int)out->matches.size() >= kMaxMatches) break;
            }
        }
        publish(std::move(out), true);
    }

    void worker(void*) {
        while (g_running.load(std::memory_order_relaxed)) {
            if (g_want.exchange(false, std::memory_order_relaxed)) build_tree();
            if (g_want_search.exchange(false, std::memory_order_relaxed)) search_snapshot();
            platform::SleepMs(25);
        }
    }

    std::unordered_set<uintptr_t> g_expanded;
    uintptr_t g_selected = 0;
    bool      g_defaults_done = false;
    char      g_filter[128] = {};
    uint64_t  g_next_auto_refresh = 0;
    std::atomic<uint64_t> g_last_panel_ms{ 0 };   // DrawPanel stamps this

    std::string label_for(uintptr_t addr) {
        const instance in{ addr };
        const std::string nm = in.get_name();
        const std::string cls = in.get_class_name();
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s  [%s]",
                      nm.empty() ? "(unnamed)" : nm.c_str(),
                      cls.empty() ? "?" : cls.c_str());
        return std::string(buf);
    }

}  // namespace

void RequestRefresh() {
    g_want.store(true, std::memory_order_relaxed);
}

void RequestSearch(const char* text) {
    {
        std::lock_guard<std::mutex> lock(g_query_mutex);
        g_query = text ? text : "";
        if (g_query.empty()) {
            // Clearing the search publishes an empty result set immediately, so
            // the window drops back to the tree without waiting for the worker.
        }
    }
    g_want_search.store(true, std::memory_order_relaxed);
}

void Run() {
    if (!g_started.exchange(true)) {
        g_running.store(true, std::memory_order_relaxed);
        platform::StartThread(worker, nullptr);
    }
    // The dex is a TAB now, and this loop does not know which tab is open. The
    // panel stamps the clock while it is being drawn, so "recently drawn" is the
    // tab being visible - no page index has to cross the UI/app boundary.
    const uint64_t now = platform::NowMs();
    if (g_last_panel_ms == 0 || now - g_last_panel_ms > 1000) return;
    // Auto-refresh while the tab is up: the DataModel changes under us when the
    // game streams in, and a browser that needs a manual refresh to show a new
    // part reads as broken.
    if (g_next_auto_refresh == 0 || (int)(now - g_next_auto_refresh) >= 0) {
        g_next_auto_refresh = now + 8000;
        RequestRefresh();
    }
}

void DrawPanel() {
    g_last_panel_ms.store(platform::NowMs(), std::memory_order_relaxed);
    Run();

    std::shared_ptr<const Snap> snap = snapshot(false);
    std::shared_ptr<const Snap> search = snapshot(true);
    const bool searching = search && !search->query.empty();

    if (!g_base_address) {
        ImGui::TextUnformatted("not attached - the DataModel is not readable yet");
        return;
    }

    if (ImGui::Button("refresh")) RequestRefresh();
    ImGui::SameLine();
    if (ImGui::Button("collapse all")) {
        g_expanded.clear();
        if (snap && !snap->nodes.empty()) g_expanded.insert(snap->nodes[0].addr);
    }
    ImGui::SameLine();
    if (snap)
        ImGui::TextDisabled("%zu objects%s", snap->nodes.size(),
                            snap->truncated ? " (capped)" : "");
    else
        ImGui::TextDisabled("walking the DataModel...");

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##filter", "filter: class or name, then Enter",
                                 g_filter, sizeof(g_filter),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        RequestSearch(g_filter);
    }
    if (searching) {
        ImGui::TextDisabled("search: %zu %s", search->matches.size(),
                            search->matches.size() >= (size_t)kMaxMatches ? "(first 500)"
                                                                           : "match(es)");
        ImGui::SameLine();
        if (ImGui::SmallButton("clear filter")) {
            g_filter[0] = '\0';
            RequestSearch("");
        }
    }

    // Default expansion: the DataModel and its services. Dex opens showing the
    // service list; opening on one collapsed node makes the tab look empty.
    if (snap && !g_defaults_done && snap->nodes.size() > 1) {
        g_defaults_done = true;
        g_expanded.insert(snap->nodes[0].addr);
        for (int i = snap->nodes[0].first; i < snap->nodes[0].last; ++i)
            if (i >= 0 && (size_t)i < snap->nodes.size())
                g_expanded.insert(snap->nodes[(size_t)i].addr);
    }

    const float footer = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
    ImGui::BeginChild("##dex-tree", ImVec2(0.0f, -footer), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (searching && search) {
        ImGuiListClipper clip;
        clip.Begin((int)search->matches.size());
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const uintptr_t addr = search->matches[(size_t)i];
                ImGui::PushID((const void*)addr);
                const std::string label = label_for(addr);
                if (ImGui::Selectable(label.c_str(), g_selected == addr)) {
                    g_selected = addr;
                    if (snap) {
                        // Expand the ancestors so the tree view can show where the
                        // hit lives after the filter is cleared.
                        uintptr_t walk = addr;
                        for (int guard = 0; guard < kMaxDepth && snap; ++guard) {
                            int found = -1;
                            for (size_t k = 0; k < snap->nodes.size(); ++k) {
                                if (snap->nodes[k].addr == walk) { found = (int)k; break; }
                            }
                            if (found < 0) break;
                            const int p = snap->nodes[(size_t)found].parent;
                            if (p < 0) break;
                            g_expanded.insert(snap->nodes[(size_t)p].addr);
                            walk = snap->nodes[(size_t)p].addr;
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        clip.End();
    } else if (snap && !snap->nodes.empty()) {
        struct Row { int node; int depth; };
        std::vector<Row> rows;
        rows.reserve(256);

        // Iterative DFS with an explicit stack: (node, next child to visit).
        struct Frame { int node; int next; };
        std::vector<Frame> stack;
        stack.push_back(Frame{ 0, snap->nodes[0].first });
        rows.push_back(Row{ 0, 0 });
        if (!g_expanded.count(snap->nodes[0].addr)) stack.clear();

        while (!stack.empty() && (int)rows.size() < kMaxRows) {
            Frame& f = stack.back();
            const Node& n = snap->nodes[(size_t)f.node];
            if (f.next >= n.last) { stack.pop_back(); continue; }
            const int child = f.next++;
            const Node& cn = snap->nodes[(size_t)child];
            rows.push_back(Row{ child, cn.depth });
            if (g_expanded.count(cn.addr) && cn.last > cn.first)
                stack.push_back(Frame{ child, cn.first });
        }

        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const Node& n = snap->nodes[(size_t)rows[(size_t)i].node];
                ImGui::PushID((const void*)n.addr);
                const bool has_kids = n.last > n.first;
                const bool open = g_expanded.count(n.addr) != 0;

                if (n.depth > 0) ImGui::Indent(12.0f * (float)n.depth);
                if (has_kids) {
                    if (ImGui::SmallButton(open ? "v" : ">")) {
                        if (open) g_expanded.erase(n.addr);
                        else      g_expanded.insert(n.addr);
                    }
                } else {
                    ImGui::Dummy(ImVec2(ImGui::GetFontSize() * 1.1f, 0.0f));
                }
                ImGui::SameLine(0.0f, 2.0f);

                const std::string label = label_for(n.addr);
                if (ImGui::Selectable(label.c_str(), g_selected == n.addr,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    g_selected = n.addr;
                    if (has_kids && ImGui::IsMouseDoubleClicked(0)) {
                        if (open) g_expanded.erase(n.addr);
                        else      g_expanded.insert(n.addr);
                    }
                }
                if (n.depth > 0) ImGui::Unindent(12.0f * (float)n.depth);
                ImGui::PopID();
            }
        }
        clip.End();

        if ((int)rows.size() >= kMaxRows)
            ImGui::TextDisabled("row limit reached - collapse some branches");
    } else {
        ImGui::TextUnformatted("no objects yet");
    }
    ImGui::EndChild();

    // --------------------------------------------------------------- details
    if (g_selected) {
        char addr[32];
        std::snprintf(addr, sizeof(addr), "0x%llX", (unsigned long long)g_selected);
        const std::string label = label_for(g_selected);
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", addr);
        if (ImGui::Button("copy address")) ImGui::SetClipboardText(addr);
        ImGui::SameLine();
        if (ImGui::Button("copy name")) {
            const std::string nm = instance{ g_selected }.get_name();
            ImGui::SetClipboardText(nm.c_str());
        }
    } else {
        ImGui::TextDisabled("select an object to see its address");
    }
    if (snap)
        ImGui::TextDisabled("walked %llu ms ago", (unsigned long long)snap->built_ms);
}

}  // namespace explorer
}  // namespace features
