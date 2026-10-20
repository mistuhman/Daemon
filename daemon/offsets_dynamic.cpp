// ---------------------------------------------------------------------------- 
//  offsets_dynamic.cpp — Linux PID-local offset patching (LINUX_PORT_PLAN §6)
// ---------------------------------------------------------------------------- 
#ifdef __linux__

#include "offsets_dynamic.h"
#include "offsets.h"
#include "platform/platform.h"
#include "platform/sober_scanner.h"
#include "globals.h"

#include "platform/offsets_zero_linux.inc"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace offsets_dynamic {
namespace {
    std::string g_diag = "offsets_dynamic: not yet tried";
    std::mutex g_mutex;
    std::atomic<bool> g_done{false};          // static stage attempted
    bool g_ok = false;                        // static stage succeeded
    std::atomic<bool> g_runtime{false};       // runtime stage succeeded
    sober::ScanResult g_result;
    uint32_t g_pid = 0;
    uintptr_t g_base = 0;
    std::string g_start;
    std::chrono::steady_clock::time_point g_next_probe{};

    // Copy every validated discovery into the Offsets:: globals the shared
    // features already read. Never copies a field the scanner left at 0.
    void ApplyStatic() {
        const sober::ScanResult& r = g_result;
        if (r.instChildrenContainer) Offsets::Instance::ChildrenStart = r.instChildrenContainer;
        if (r.instChildrenArray)     Offsets::Instance::ChildrenArray = r.instChildrenArray;
        if (r.instChildrenEnd)       Offsets::Instance::ChildrenEnd = r.instChildrenEnd;
        if (r.instChildrenStride)    Offsets::Instance::ChildrenStride = r.instChildrenStride;
        if (r.instClassDescriptor)   Offsets::Instance::ClassDescriptor = r.instClassDescriptor;
        if (r.instClassName)         Offsets::Instance::ClassName = r.instClassName;
        if (r.instNameContainer)     Offsets::Instance::NameContainer = r.instNameContainer;
        if (r.instName)              Offsets::Instance::Name = r.instName;
        if (r.instParent)            Offsets::Instance::Parent = r.instParent;
        if (r.wsCurrentCamera)       Offsets::Workspace::CurrentCamera = r.wsCurrentCamera;
        if (r.playersLocalPlayer)    Offsets::Player::LocalPlayer = r.playersLocalPlayer;
        if (r.playerModelInstance)   Offsets::Player::ModelInstance = r.playerModelInstance;
        if (r.playerTeam)            Offsets::Player::Team = r.playerTeam;
        if (r.visualEngineGlobal && g_base &&
            r.visualEngineGlobal >= g_base) {
            Offsets::VisualEngine::Pointer = r.visualEngineGlobal - g_base;
        }
    }

    void ApplyRuntime() {
        const sober::ScanResult& r = g_result;
        if (r.visualEngineDims) Offsets::VisualEngine::Dimensions = r.visualEngineDims;
        if (r.cameraRotation)    Offsets::Camera::Rotation = r.cameraRotation;
        if (r.cameraPosition)    Offsets::Camera::Position = r.cameraPosition;
        if (r.cameraFov)         Offsets::Camera::FieldOfView = r.cameraFov;
        if (r.basePartPrimitive) Offsets::BasePart::Primitive = r.basePartPrimitive;
        if (r.humanoidRootPart)  Offsets::Humanoid::HumanoidRootPart = r.humanoidRootPart;
        if (r.humanoidHealth) {
            Offsets::Humanoid::Health = r.humanoidHealth;
            Offsets::Humanoid::MaxHealth = r.humanoidMaxHealth;
        }
        if (r.primMotionBlock && r.primStride) {
            // Block deltas are the ARCHITECTURE 4.0 intra-block shape the
            // scanner validated as part of the pattern — recorded here as
            // derived values, satisfying mover::LayoutSane by construction.
            const uintptr_t a = r.primMotionBlock;
            const uintptr_t s = r.primStride;
            Offsets::Primitive::MotionBlock  = a;
            Offsets::Primitive::Rotation     = a + 0x00;
            Offsets::Primitive::Position     = a + 0x24;
            Offsets::Primitive::AssemblyLinearVelocity  = a + 0x30;
            Offsets::Primitive::AssemblyAngularVelocity = a + 0x3c;
            Offsets::Primitive::MotionStride = s;
            Offsets::Primitive::MotionSize   = s;
            Offsets::Primitive::CFrame2                    = a + s;
            Offsets::Primitive::Position2                  = a + s + 0x24;
            Offsets::Primitive::AssemblyLinearVelocity2    = a + s + 0x30;
            Offsets::Primitive::AssemblyAngularVelocity2   = a + s + 0x3c;
            Offsets::Primitive::BlockRotation = 0x00;
            Offsets::Primitive::BlockPosition = 0x24;
            Offsets::Primitive::BlockLinear   = 0x30;
            Offsets::Primitive::BlockAngular  = 0x3c;
        }
    }
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
    g_result.staticOk = g_result.dataModel && g_result.workspace;
    g_ok = g_result.staticOk;
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

    const bool same_target = pid && base && pid == g_pid && base == g_base &&
                             start == g_start;
    if (g_done.load() && same_target && g_result.staticOk) {
        // Static stage done for this process: keep inviting the runtime stage
        // (camera/motion need a spawned place, so it lands seconds later).
        if (!g_runtime.load() && !g_result.runtimeOk) {
            void* h = nullptr;
            if (platform::OpenGameHandle(pid, &h)) {
                sober::Refine(h, pid, base, g_result);
                platform::CloseHandle(h);
                if (g_result.runtimeOk) {
                    ApplyRuntime();
                    g_runtime.store(true);
                    LogLine("offsets_dynamic: runtime stage ok — camera/motion/fields discovered");
                }
                g_diag = g_result.diag;
            }
        }
        return g_ok;
    }

    g_ok = false;
    g_runtime.store(false);
    g_result = sober::ScanResult{};
    g_done = false;
    if (!pid) {
        g_diag = "offsets_dynamic: no Sober pid (waiting for attach)";
        return g_ok;
    }
    if (!base) {
        g_diag = "offsets_dynamic: no libRoblox base for pid " + std::to_string(pid);
        return g_ok;
    }
    void* h = nullptr;
    if (!platform::OpenGameHandle(pid, &h)) {
        g_diag = "offsets_dynamic: OpenGameHandle failed for pid " + std::to_string(pid);
        return g_ok;
    }

    // From here the Windows numbers are gone: discovery fills only what it
    // validated. (A fresh process must never inherit an old process's layout.)
    ZeroAll();
    sober::ResetScanner();
    sober::SetDecodePid(pid);
    auto res = sober::Scan(h, pid, base);
    platform::CloseHandle(h);
    g_result = res;
    g_diag = res.diag;
    if (!res.diag.empty()) LogLine("offsets_dynamic: %s", res.diag.c_str());
    if (res.ok()) {
        ApplyStatic();
        g_ok = true;
        LogLine("offsets_dynamic: static ok — dm 0x%llx ws 0x%llx %s stride %llu, "
                "descriptor %s, camera %s",
                (unsigned long long)res.dataModel,
                (unsigned long long)res.workspace,
                res.visualEngine ? "ve," : "",
                (unsigned long long)res.instChildrenStride,
                res.instClassDescriptor ? "yes" : "RTTI",
                res.wsCurrentCamera ? "member" : "pending");
    } else {
        LogLine("offsets_dynamic: unresolved Linux layout — no Windows fallback");
        g_ok = false;
        sober::SetDecodePid(0);
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
bool StaticReady() { return g_ok; }
bool RuntimeReady() { return g_runtime.load(); }

void Invalidate() {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_done = false;
    g_ok = false;
    g_runtime.store(false);
    g_diag = "offsets_dynamic: invalidated (will re-scan)";
    g_result = sober::ScanResult{};
    g_pid = 0; g_base = 0; g_start.clear(); g_next_probe = {};
    sober::ResetScanner();
    sober::SetDecodePid(0);
    ZeroAll();
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
