// ----------------------------------------------------------------------------
//  offsets_dynamic.cpp — Linux PID-local offset patching (LINUX_PORT_PLAN §6)
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "offsets_dynamic.h"
#include "offsets.h"
#include "platform/platform.h"
#include "platform/sober_scanner.h"
#include "globals.h"

#include <string>
#include <mutex>
#include <atomic>
#include <chrono>

namespace offsets_dynamic {
namespace {
    std::string g_diag = "offsets_dynamic: not yet tried";
    std::mutex g_mutex;
    std::atomic<bool> g_done{false};
    bool g_ok = false;
    sober::ScanResult g_result;
    uint32_t g_pid = 0;
    uintptr_t g_base = 0;
    std::string g_start;
    std::chrono::steady_clock::time_point g_next_probe{};
} // namespace

bool PatchFrom(const Patch& p) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (p.dataModel) g_result.dataModel = p.dataModel;
    if (p.workspace) g_result.workspace = p.workspace;
    if (p.world) g_result.world = p.world;
    if (p.primitives) g_result.primitives = p.primitives;
    if (p.primRotation) Offsets::Primitive::Rotation = p.primRotation;
    if (p.primFlags) Offsets::Primitive::Flags = p.primFlags;
    g_diag = "offsets_dynamic: patched from injected Patch";
    g_ok = true;
    g_done = true;
    return true;
}

bool Ensure() {
    std::lock_guard<std::mutex> lk(g_mutex);
    const auto now = std::chrono::steady_clock::now();
    if (now < g_next_probe) return g_ok;
    g_next_probe = now + std::chrono::milliseconds(500);
    uint32_t pid = platform::FindGameProcess();
    const auto start = sober::ProcessStartTicks(pid);
    const auto base = pid ? platform::GameBaseAddress(pid) : 0;
    if (g_done.load() && pid && base && pid == g_pid && base == g_base && start == g_start) return g_ok;
    g_ok = false;
    g_result = {};
    g_done = false;
    if (!pid) {
        g_diag = "offsets_dynamic: no Sober pid (waiting for attach)";
        g_ok = false;
        // Transient attach failure must be retried after launch/restart.
        return g_ok;
    }
    if (!base) {
        g_diag = "offsets_dynamic: no libRoblox base for pid " + std::to_string(pid);
        g_ok = false;
        // Transient attach failure must be retried after launch/restart.
        return g_ok;
    }
    void* h = nullptr;
    if (!platform::OpenGameHandle(pid, &h)) {
        g_diag = "offsets_dynamic: OpenGameHandle failed for pid " + std::to_string(pid);
        g_ok = false;
        // Transient attach failure must be retried after launch/restart.
        return g_ok;
    }
    auto res = sober::Scan(h, pid, base);
    platform::CloseHandle(h);
    g_result = res;
    g_diag = res.diag;
    if (!res.diag.empty()) LogLine("offsets_dynamic: %s", res.diag.c_str());
    if (res.ok()) {
        if (res.primitiveRotation) Offsets::Primitive::Rotation = res.primitiveRotation;
        if (res.primitiveFlags) Offsets::Primitive::Flags = res.primitiveFlags;
        if (res.primitivePosition) Offsets::Primitive::Position = res.primitivePosition;
        LogLine("offsets_dynamic: OK task 0x%llx data 0x%llx ws 0x%llx world 0x%llx",
                (unsigned long long)res.taskScheduler,
                (unsigned long long)res.dataModel,
                (unsigned long long)res.workspace,
                (unsigned long long)res.world);
        g_ok = true;
    } else {
        LogLine("offsets_dynamic: unresolved Linux layout — no Windows fallback");
        g_ok = false;
    }
    g_pid = pid; g_base = base; g_start = start;
    g_done = true;
    return g_ok;
}

const std::string& diag() {
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_diag;
}
const sober::ScanResult& GetScanResult() { return g_result; }
uintptr_t GetDataModel() { return g_result.dataModel; }

void Invalidate() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_done = false;
    g_ok = false;
    g_diag = "offsets_dynamic: invalidated (will re-scan)";
    g_result = sober::ScanResult{};
    g_pid = 0; g_base = 0; g_start.clear(); g_next_probe = {};
}

} // namespace offsets_dynamic

#else // Windows

#include "offsets_dynamic.h"
namespace offsets_dynamic {
bool Ensure() { return false; }
const std::string& diag() { static std::string s="windows: use offsets_live"; return s; }
void Invalidate() {}
bool PatchFrom(const Patch&) { return false; }
}

#endif
