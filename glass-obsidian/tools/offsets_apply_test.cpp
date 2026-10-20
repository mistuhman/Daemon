// -----------------------------------------------------------------------------
//  tools/offsets_apply_test.cpp  --  the offset-dump parsers, off Windows
// -----------------------------------------------------------------------------
//   g++ -std=c++17 -O1 -w -I"../roblox_external" -I"tools/win32_stub" \
//       tools/offsets_apply_test.cpp -o build/offsets_apply_test
//   ./build/offsets_apply_test
//
// Includes the real offsets_live.cpp (not a copy), so what is pinned here is
// the code the shipped exe runs: the imtheo JSON parser, the Offsetmanager
// header parser, the format sniffer, the version extractor, and - by reading
// the repo's own offsets_fallback.json - the exact file every build now ships
// next to the exe. The fixture bodies below are kept close to the real wire
// text for version-c5aecda2245e4fae (they only have to be parseable, so they
// stay put across updates); the SHIPPED-fallback block at the bottom is re-pinned
// by tools/offsets_regen.py's output whenever offsets.h moves to a new version.
//
// RefreshOffsets() is deliberately not called: it is network + files, and the
// point of this file is that the parsing half is verifiable without either.
// -----------------------------------------------------------------------------
#include "features/servers/servers_http.h"
#include "globals.h"

#include "../roblox_external/offsets_live.cpp"

#include <cstdio>
#include <string>

// offsets_live.cpp reaches platform::ExeDir for its cache paths; the
// canned platform answers with the test's working directory (the flow
// test runs each scenario in its own scratch dir, so ExeDir must not
// point at the real binary's home).
namespace platform {
std::string ExeDir() { return "./"; }
bool Init() { return true; }
} // namespace platform


// offsets_live.cpp only needs these to link; the test never lets them run.
// Signatures match servers_http.h (minus the default arguments).
namespace features { namespace servershttp {
bool Get(const char*, std::string&, std::string&, const char*) { return false; }
bool GetViaCurl(const char*, std::string&, std::string&, const char*) { return false; }
bool GetAny(const char*, std::string&, std::string&, const char*) { return false; }
bool Open(const char*, std::string&) { return false; }
bool TryInInstanceHop(const char*, unsigned, std::string&) { return false; }
} }

static int g_fail = 0, g_checks = 0;
static void Ok(bool cond, const char* what)
{
    ++g_checks;
    if (!cond) { ++g_fail; printf("  [FAIL] %s\n", what); }
}
static void EqU64(uint64_t got, uint64_t want, const char* what)
{
    ++g_checks;
    if (got != want) { ++g_fail; printf("  [FAIL] %s\n         got:  0x%llx\n         want: 0x%llx\n",
                                        what, (unsigned long long)got, (unsigned long long)want); }
}
static void EqStr(const std::string& got, const char* want, const char* what)
{
    ++g_checks;
    if (got != want) { ++g_fail; printf("  [FAIL] %s\n         got:  \"%s\"\n         want: \"%s\"\n",
                                        what, got.c_str(), want); }
}

// A slice of the real Offsetmanager External_Offsets.hpp for
// version-c5aecda2245e4fae, kept close to verbatim (spacing included) - this
// is the wire format the "offsetmanager" source feeds ApplyOffsetsBody.
static const char* kHpp =
"#pragma once\n"
"#include <cstdint>\n"
"#include <string>\n"
"namespace Offsets {\n"
"    inline std::string ClientVersion = \"version-c5aecda2245e4fae\";\n"
"\n"
"    namespace TaskScheduler {\n"
"         inline constexpr uintptr_t Pointer = 0x8B5CEE8;\n"
"         inline constexpr uintptr_t JobStart = 0xC8;\n"
"         inline constexpr uintptr_t JobEnd = 0xD0;\n"
"         inline constexpr uintptr_t JobName = 0x18;\n"
"         inline constexpr uintptr_t MaxFPS = 0xB0;\n"
"    }\n"
"\n"
"    namespace VisualEngine {\n"
"         inline constexpr uintptr_t Pointer = 0x83F0A08;\n"
"         inline constexpr uintptr_t Dimensions = 0xB10;\n"
"         inline constexpr uintptr_t ViewMatrix = 0x1B0;\n"
"    }\n"
"\n"
"    namespace Humanoid {\n"
"         inline constexpr uintptr_t WalkspeedCheck = 0x3B4;\n"
"         inline constexpr uintptr_t IsWalking = 0x95F;\n"
"    }\n"
"\n"
"    namespace RenderView {\n"
"         inline constexpr uintptr_t LightingValid = 0x0;\n"
"    }\n"
"}\n";

// The imtheo JSON shape: metadata + one nested object of class objects. The
// "Player" class exercises fields the tool tracks; the "NotAClass" one fields
// it does not (they must be skipped without complaint).
static const char* kJson =
"{\"Source\":\"https://offsets.imtheo.lol\",\"Roblox Version\":\"version-c5aecda2245e4fae\","
"\"Dumper Version\":\"2.2.3\",\"Total Offsets\":392,\"Offsets\":{"
"\"TaskScheduler\":{\"Pointer\":146132712,\"JobStart\":200,\"JobEnd\":208,\"JobName\":24,\"MaxFPS\":176},"
"\"VisualEngine\":{\"Pointer\":138349064,\"Dimensions\":2832,\"ViewMatrix\":432},"
"\"Humanoid\":{\"WalkspeedCheck\":948,\"IsWalking\":2399},"
"\"NotAClass\":{\"Bogus\":1,\"Pointer\":7}"
"}}";

// What r.jina.ai puts in front of a JSON reply ("Markdown Content:" header).
static const char* kJina =
"Title: \n"
"URL Source: https://offsets.imtheo.lol/offsets.json\n"
"Markdown Content:\n";

// The two JSON version-mirror shapes, plus the reader-wrapped text endpoint.
static const char* kRddVersion =
"{\"Windows\":\"version-c5aecda2245e4fae\",\"WindowsDate\":\"9/9/2026, 10:30:28 PM UTC\"}";
static const char* kWeaoVersion =
"{\n  \"fetchedAt\": \"2026-09-09T23:55:01.561Z\",\n  \"data\": {\n    \"current\": {\n"
"      \"Windows\": \"version-c5aecda2245e4fae\",\n      \"WindowsDate\": \"x\",\n"
"      \"WindowsResponse\": {\"clientVersionUpload\": \"version-c5aecda2245e4fae\"}\n"
"}}}";
static const char* kJinaVersion =
"Title: \nURL Source: https://offsets.imtheo.lol/roblox/version\nMarkdown Content:\nversion-c5aecda2245e4fae\n";

// Reads the repo's shipped fallback (run from glass-obsidian/ or from tools/).
static bool ReadRepoFallback(std::string& out)
{
    const char* paths[] = { "../roblox_external/offsets_fallback.json",
                            "roblox_external/offsets_fallback.json",
                            "../../roblox_external/offsets_fallback.json" };
    for (const char* p : paths)
        if (ReadFile(p, out) && !out.empty()) return true;
    return false;
}

int main()
{
    // ---- the header parser, directly --------------------------------------
    {
        Offsets::TaskScheduler::Pointer = 1;
        Offsets::VisualEngine::Pointer = 1;
        Offsets::Humanoid::WalkspeedCheck = 1;
        Offsets::Humanoid::IsWalking = 1;
        Offsets::RenderView::LightingValid = 0x150;   // dump-zero must survive

        int applied = 0, kept = 0;
        Ok(ApplyOffsetsHpp(kHpp, applied, kept), "hpp parses");
        EqU64(Offsets::TaskScheduler::Pointer, 0x8B5CEE8, "hpp TaskScheduler::Pointer");
        EqU64(Offsets::VisualEngine::Pointer, 0x83F0A08, "hpp VisualEngine::Pointer");
        EqU64(Offsets::Humanoid::WalkspeedCheck, 0x3B4, "hpp Humanoid::WalkspeedCheck");
        EqU64(Offsets::Humanoid::IsWalking, 0x95F, "hpp Humanoid::IsWalking");
        EqU64(Offsets::RenderView::LightingValid, 0x150, "hpp dump-zero keeps compiled default");
        EqStr(Offsets::ClientVersion, "version-c5aecda2245e4fae", "hpp ClientVersion");
        EqU64((uint64_t)applied, 10, "hpp applied count");
        EqU64((uint64_t)kept, 1, "hpp kept-zero count");
    }

    // ---- the JSON parser, via the sniffer ---------------------------------
    {
        Offsets::TaskScheduler::Pointer = 1;
        Offsets::VisualEngine::Pointer = 1;
        Offsets::Humanoid::WalkspeedCheck = 1;
        Offsets::DataModel::JobId = 1;
        Offsets::DataModel::PlaceId = 1;

        const char* fmt = nullptr;
        int applied = 0, kept = 0;
        Ok(ApplyOffsetsBody(kJson, fmt, applied, kept), "json parses");
        EqStr(fmt ? fmt : "", "json", "json sniffed as json");
        EqU64(Offsets::TaskScheduler::Pointer, 146132712, "json TaskScheduler::Pointer");
        EqU64(Offsets::VisualEngine::Pointer, 138349064, "json VisualEngine::Pointer");
        EqU64(Offsets::Humanoid::WalkspeedCheck, 948, "json Humanoid::WalkspeedCheck");
        EqStr(Offsets::ClientVersion, "version-c5aecda2245e4fae", "json ClientVersion");
        EqU64((uint64_t)applied, 10, "json applied count (unknown classes skipped)");
    }

    // ---- the reader-proxy preamble is stripped ----------------------------
    {
        Offsets::VisualEngine::Pointer = 1;
        const char* fmt = nullptr;
        int applied = 0, kept = 0;
        Ok(ApplyOffsetsBody(std::string(kJina) + kJson, fmt, applied, kept), "jina body parses");
        EqStr(fmt ? fmt : "", "json", "jina body sniffed as json");
        EqU64(Offsets::VisualEngine::Pointer, 138349064, "jina VisualEngine::Pointer");
    }

    // ---- the sniffer picks the hpp format too -----------------------------
    {
        const char* fmt = nullptr;
        int applied = 0, kept = 0;
        Ok(ApplyOffsetsBody(kHpp, fmt, applied, kept), "body sniffs hpp");
        EqStr(fmt ? fmt : "", "hpp", "body sniffed as hpp");
    }

    // ---- garbage is refused, not shrugged at ------------------------------
    {
        const char* fmt = nullptr;
        int applied = 0, kept = 0;
        Ok(!ApplyOffsetsBody("<html><head></head><body>challenge</body></html>", fmt, applied, kept),
           "an HTML challenge page is refused");
        Ok(!ApplyOffsetsBody("", fmt, applied, kept), "an empty body is refused");
    }

    // ---- version extraction, all three shapes -----------------------------
    {
        EqStr(ExtractVersion("version-c5aecda2245e4fae\n"), "version-c5aecda2245e4fae", "version: bare text");
        EqStr(ExtractVersion("version-c5aecda2245e4fae"), "version-c5aecda2245e4fae", "version: bare, no newline");
        EqStr(ExtractVersion(kRddVersion), "version-c5aecda2245e4fae", "version: rdd json");
        EqStr(ExtractVersion(kWeaoVersion), "version-c5aecda2245e4fae", "version: weao json");
        EqStr(ExtractVersion(kJinaVersion), "version-c5aecda2245e4fae", "version: reader-wrapped text");
        Ok(ExtractVersion("{\"Windows\":\"0.738.0.7381397\"}").empty(), "version: a semver is not a version hash");
        Ok(ExtractVersion("").empty(), "version: empty body");
    }

    // ---- the repo's own shipped fallback, end to end ----------------------
    {
        std::string body;
        if (!ReadRepoFallback(body)) {
            Ok(false, "read the repo's offsets_fallback.json (run from glass-obsidian/ or tools/)");
        } else {
            const char* fmt = nullptr;
            int applied = 0, kept = 0;
            Ok(ApplyOffsetsBody(body, fmt, applied, kept), "shipped fallback parses");
            EqStr(fmt ? fmt : "", "json", "shipped fallback sniffed as json");
            EqStr(Offsets::ClientVersion, "version-4310300497aa4917", "shipped fallback version");
            // the critical-path offsets, from the 431030 dump
            EqU64(Offsets::TaskScheduler::Pointer, 0x8BDD8E8, "fallback TaskScheduler::Pointer");
            EqU64(Offsets::VisualEngine::Pointer, 0x846F768, "fallback VisualEngine::Pointer");
            EqU64(Offsets::FakeDataModel::Pointer, 0x8E42C98, "fallback FakeDataModel::Pointer");
            EqU64(Offsets::DataModel::JobId, 0x120, "fallback DataModel::JobId");
            EqU64(Offsets::DataModel::PlaceId, 0x198, "fallback DataModel::PlaceId");
            EqU64(Offsets::Instance::ChildrenStart, 0x78, "fallback Instance::ChildrenStart");
            EqU64(Offsets::Player::LocalPlayer, 0x130, "fallback Player::LocalPlayer");
            EqU64(Offsets::Humanoid::WalkspeedCheck, 0x3B4, "fallback Humanoid::WalkspeedCheck");
            EqU64(Offsets::Humanoid::HumanoidRootPart, 0x470, "fallback Humanoid::HumanoidRootPart");
            EqU64(Offsets::Humanoid::Walkspeed, 0x1D0, "fallback Humanoid::Walkspeed");
            EqU64(Offsets::Camera::Position, 0xFC, "fallback Camera::Position");
            EqU64(Offsets::Workspace::CurrentCamera, 0x4B8, "fallback Workspace::CurrentCamera");
            EqU64(Offsets::Workspace::World, 0x410, "fallback Workspace::World");
            // Block B of the primitive is derived, never dumped, and every position
            // write in the app depends on it - so it is pinned here as loudly as the
            // offsets a dumper actually provides: A at 0xB0, +0x48 stride.
            EqU64(Offsets::Primitive::Rotation, 0xB0, "fallback Primitive::Rotation");
            EqU64(Offsets::Primitive::Position, 0xD4, "fallback Primitive::Position");
            EqU64(Offsets::Primitive::CFrame2, 0xF8, "fallback derived block B rotation");
            EqU64(Offsets::Primitive::Position2, 0x11C, "fallback derived block B position");
            // The fields the dumper reports as 0 for this version must still carry a
            // number: a zero here is a read at address+0, and for RenderView's two
            // validity bytes it is a WRITE at address+0. That is what "kept" means.
            EqU64(Offsets::RenderView::LightingValid, 0x150, "fallback keeps RenderView::LightingValid");
            EqU64(Offsets::RenderView::SkyValid, 0x28D, "fallback keeps RenderView::SkyValid");
            EqU64(Offsets::GuiBase2D::AbsoluteSize, 0x114, "fallback keeps GuiBase2D::AbsoluteSize");
            Ok(applied >= 250, "fallback applies the whole dump, not a subset");
        }
    }

    // ---- offsets.h vs the fallback it ships beside -------------------------
    // Both carry the same release's numbers, and a paste of a dumper header is the way
    // they stop agreeing: it replaces the app's longer field list with the dump's shorter
    // one and declares the values constexpr, which is the form a dumper writes and the
    // one offset_live.cpp cannot compile against. Read as text, because the compiled
    // globals are exactly what a successful parse overwrites.
    {
        std::string hdr;
        const char* paths[] = { "../roblox_external/offsets.h", "roblox_external/offsets.h",
                                "../../roblox_external/offsets.h" };
        for (const char* p : paths)
            if (ReadFile(p, hdr) && !hdr.empty()) break;
        Ok(!hdr.empty(), "read offsets.h");
        if (!hdr.empty()) {
            // "constexpr uintptr_t", not "constexpr": the file's own header comment says
            // the word while explaining why the declarations must not use it.
            Ok(hdr.find("constexpr uintptr_t") == std::string::npos,
               "offsets.h declares no constexpr values (the live fetch writes into them)");
            // the fields no dumper emits, and the ones it zeroes for this release
            for (const char* key : { "MeshContentProvider", "MotionStride", "CFrame2",
                                     "AssemblyLinearVelocity2", "LightingValid" })
                Ok(hdr.find(key) != std::string::npos,
                   (std::string("offsets.h still declares ") + key + " (a paste deletes it)").c_str());
            const size_t q = hdr.find("ClientVersion = \"");
            Ok(q != std::string::npos, "offsets.h states its version");
            if (q != std::string::npos) {
                const size_t a = q + 17, b = hdr.find('"', a);
                EqStr(hdr.substr(a, b - a), "version-4310300497aa4917",
                      "offsets.h and the shipped fallback name the same version");
            }
        }
    }

    printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
