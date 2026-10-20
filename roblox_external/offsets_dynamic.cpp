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

namespace offsets_dynamic {
namespace {
    std::string g_diag = "offsets_dynamic: not yet tried";
    std::mutex g_mutex;
    std::atomic<bool> g_done{false};
    bool g_ok = false;
    sober::ScanResult g_result;
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
    if (g_done.load()) return g_ok;
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_done.load()) return g_ok;

    uint32_t pid = platform::FindGameProcess();
    if (!pid) {
        g_diag = "offsets_dynamic: no Sober pid (waiting for attach)";
        g_ok = false;
        g_done = true;
        return g_ok;
    }
    uintptr_t base = platform::GameBaseAddress(pid);
    if (!base) {
        g_diag = "offsets_dynamic: no libRoblox base for pid " + std::to_string(pid);
        g_ok = false;
        g_done = true;
        return g_ok;
    }
    void* h = nullptr;
    if (!platform::OpenGameHandle(pid, &h)) {
        g_diag = "offsets_dynamic: OpenGameHandle failed for pid " + std::to_string(pid);
        g_ok = false;
        g_done = true;
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
        LogLine("offsets_dynamic: scan not ok — fallback to offsets.h");
        g_ok = false;
    }
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
