// ----------------------------------------------------------------------------
//  platform/sober_scanner.cpp — PID-local offset discovery for Sober
// ----------------------------------------------------------------------------
//  Attachment uses shared read-only ELF inspection, not comm/cgroup ranking.
//  Object layout is DISCOVERED from the live image in two stages:
//
//    Scan()    static: anchor -> DataModel -> instance layout -> members
//    Refine()  runtime: camera numerics -> motion blocks -> character members
//    (string/class decode for memory.cpp: TryPooledString / RttiClassName)
//
//  Methods and their provenance (docs/LINUX_ATTACH_DIAGNOSTIC.md V4):
//    * RTTI chain + member-by-class scan: the established Sober dumper
//      BitdancerJewishEdition/orchestrator (process.hpp by @h6di) — vtable-8
//      typeinfo, typeinfo+8 mangled name, member scan for a pointer whose
//      pointee's RTTI names the class.
//    * /proc maps + process_vm_readv + wildcard signature: LINUX_PORT_PLAN
//      5.3 (the Cheat Engine method, implemented natively).
//    * dual motion block + matrix/delta pattern: this repo's own measured
//      finding, ARCHITECTURE 4.0 — re-derived here, never assumed.
//  No Windows numbers are consulted anywhere in this file.
// ----------------------------------------------------------------------------

#include "sober_scanner.h"

#ifdef __linux__

#include "linux_elf.h"
#include "platform/platform.h"
#include <sys/stat.h>
#include <sys/uio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace sober {

namespace {

// --- helpers ---------------------------------------------------------------

std::string TrimLeft(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return s.substr(i);
}

// Parse a single /proc/<pid>/maps line without sscanf.
// Format: "00400000-00409000 r-xp 00000000 08:01 12345 /path"
bool ParseMapsLine(const std::string& line, uintptr_t& start, uintptr_t& end,
                   std::string& perms, std::string& path) {
    start = 0; end = 0; perms.clear(); path.clear();
    size_t dash = line.find('-');
    if (dash == std::string::npos) return false;
    size_t sp1 = line.find(' ', dash);
    if (sp1 == std::string::npos) return false;
    try {
        start = (uintptr_t)std::stoull(line.substr(0, dash), nullptr, 16);
        end   = (uintptr_t)std::stoull(line.substr(dash + 1, sp1 - dash - 1), nullptr, 16);
    } catch (...) { return false; }
    size_t p1 = line.find_first_not_of(' ', sp1);
    if (p1 == std::string::npos) return false;
    size_t p2 = line.find(' ', p1);
    if (p2 == std::string::npos) return false;
    perms = line.substr(p1, p2 - p1);
    size_t cur = p2;
    for (int i = 0; i < 3; ++i) {
        cur = line.find_first_not_of(' ', cur);
        if (cur == std::string::npos) return true;
        size_t nxt = line.find(' ', cur);
        if (nxt == std::string::npos) return true;
        cur = nxt;
    }
    cur = line.find_first_not_of(' ', cur);
    if (cur != std::string::npos) path = TrimLeft(line.substr(cur));
    return true;
}

// --- remote reads: process_vm_readv, the diagnostic's own path -------------

bool RD(uint32_t pid, uintptr_t addr, void* out, size_t n) {
    if (!addr || n > UINTPTR_MAX - addr) return false;
    struct iovec local{out, n}, remote{reinterpret_cast<void*>(addr), n};
    return process_vm_readv(pid, &local, 1, &remote, 1, 0) == (ssize_t)n;
}

template <typename T> bool RV(uint32_t pid, uintptr_t addr, T& out) {
    return RD(pid, addr, &out, sizeof(T));
}

// Batched 8-byte reads — one vectored syscall per kBBatch targets. The VE
// anchor probes every plausible pointer in the image's data segments;
// per-pointer syscalls would spend seconds there, this spends tens of ms.
constexpr size_t kBBatch = 256;
void RVectored(uint32_t pid, const std::vector<uintptr_t>& addrs,
               std::vector<uintptr_t>& out) {
    out.assign(addrs.size(), 0);
    size_t i = 0;
    while (i < addrs.size()) {
        const size_t n = std::min(kBBatch, addrs.size() - i);
        std::vector<struct iovec> loc(n), rem(n);
        for (size_t k = 0; k < n; ++k) {
            loc[k].iov_base = &out[i + k];
            loc[k].iov_len = sizeof(uintptr_t);
            rem[k].iov_base = reinterpret_cast<void*>(addrs[i + k]);
            rem[k].iov_len = sizeof(uintptr_t);
        }
        const ssize_t got = process_vm_readv(pid, loc.data(), (unsigned long)n,
                                             rem.data(), (unsigned long)n, 0);
        if (got != (ssize_t)(n * sizeof(uintptr_t))) {
            // stale candidate in a hole, or partial: per-address fallback
            for (size_t k = 0; k < n; ++k)
                if (!RV(pid, addrs[i + k], out[i + k])) out[i + k] = 0;
        }
        i += n;
    }
}

bool ReadCString(uint32_t pid, uintptr_t addr, std::string& out, size_t max = 256) {
    out.clear();
    if (!addr) return false;
    char buf[256];
    if (max > sizeof(buf)) max = sizeof(buf);
    if (!RD(pid, addr, buf, max)) return false;
    for (size_t i = 0; i < max; ++i) {
        if (!buf[i]) { out.assign(buf, i); return true; }
        out.push_back(buf[i]);
    }
    return true; // unterminated but read; caller validates
}

bool PlausibleHeap(uintptr_t v) {
    if (v < 0x100000000000ull || v >= 0x800000000000ull) return false;
    if (v & 7) return false;
    return true;
}

bool Printable(const std::string& s) {
    if (s.empty() || s.size() > 4096) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

// --- Itanium RTTI ----------------------------------------------------------

// "_ZTS..." -> last name component. Handles _ZTS9DataModel and
// _ZTSN5Roblox9DataModelE; rejects vendor/template noise instead of guessing.
bool ParseZts(const std::string& mangled, std::string& out) {
    out.clear();
    if (mangled.rfind("_ZTS", 0) != 0) return false;
    size_t i = 4;
    auto parse_comp = [&](std::string& comp) -> bool {
        comp.clear();
        if (i >= mangled.size() || !std::isdigit((unsigned char)mangled[i])) return false;
        size_t n = 0;
        while (i < mangled.size() && std::isdigit((unsigned char)mangled[i])) {
            n = n * 10 + (mangled[i] - '0');
            if (n > 4096) return false;
            ++i;
        }
        if (!n || i + n > mangled.size()) return false;
        comp = mangled.substr(i, n);
        i += n;
        for (char c : comp)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == ':'))
                return false;
        return true;
    };
    if (i < mangled.size() && mangled[i] == 'N') {
        ++i;
        if (i < mangled.size() && (mangled[i] == 'S' || mangled[i] == 'K')) return false;
        std::string last, comp;
        while (i < mangled.size() && mangled[i] != 'E') {
            if (!parse_comp(comp)) return false;
            last = comp;
        }
        if (last.empty()) return false;
        out = last;
        return true;
    }
    std::string comp;
    if (!parse_comp(comp)) return false;
    out = comp;
    return true;
}

// obj -> vtable -> typeinfo(-8) -> name(+8) -> "_ZTS...". Raw, unvalidated
// beyond "the chain reads".
bool RttiRaw(uint32_t pid, uintptr_t obj, std::string& mangled) {
    mangled.clear();
    uintptr_t vtable = 0;
    if (!RV(pid, obj, vtable) || vtable < 0x10000 || (vtable & 7)) return false;
    uintptr_t typeinfo = 0;
    if (!RV(pid, vtable - 8, typeinfo) || typeinfo < 0x10000 || (typeinfo & 7)) return false;
    uintptr_t namep = 0;
    if (!RV(pid, typeinfo + 8, namep) || namep < 0x10000) return false;
    return ReadCString(pid, namep, mangled, 256) && mangled.rfind("_ZTS", 0) == 0;
}

// Last-component token of an object's RTTI class name. "" on failure.
std::string RttiToken(uint32_t pid, uintptr_t obj) {
    std::string mangled, token;
    if (!RttiRaw(pid, obj, mangled)) return {};
    if (ParseZts(mangled, token)) return token;
    return mangled; // unparsable but real — substring matching still works
}

// orchestrator's find_class_offset, ours: members of `obj` (8-byte stride)
// whose pointee's RTTI names the class. Returns hit count.
int FindMembersByToken(uint32_t pid, uintptr_t obj, const char* token,
                       uintptr_t* first_off, uintptr_t* first_val,
                       uintptr_t limit = 0x1500) {
    int hits = 0;
    if (!obj) return 0;
    for (uintptr_t off = 0; off + 8 <= limit; off += 8) {
        uintptr_t p = 0;
        if (!RV(pid, obj + off, p) || !PlausibleHeap(p)) continue;
        std::string t = RttiToken(pid, p);
        if (t.empty() || t.find(token) == std::string::npos) continue;
        if (first_off && hits == 0) *first_off = off;
        if (first_val && hits == 0) *first_val = p;
        ++hits;
    }
    return hits;
}

// Exact-token variant (used where substring ambiguity is costly: Camera).
int FindMembersByExactToken(uint32_t pid, uintptr_t obj, const char* token,
                            uintptr_t* first_off, uintptr_t* first_val,
                            uintptr_t limit = 0x1500) {
    int hits = 0;
    if (!obj) return 0;
    for (uintptr_t off = 0; off + 8 <= limit; off += 8) {
        uintptr_t p = 0;
        if (!RV(pid, obj + off, p) || !PlausibleHeap(p)) continue;
        if (RttiToken(pid, p) != token) continue;
        if (first_off && hits == 0) *first_off = off;
        if (first_val && hits == 0) *first_val = p;
        ++hits;
    }
    return hits;
}

// --- float pattern checks (motion block / camera) --------------------------

bool F32s(uint32_t pid, uintptr_t addr, float* out, int n) {
    return RD(pid, addr, out, (size_t)n * 4);
}

bool Orthonormal3x3(const float m[9]) {
    float c0[3] = {m[0], m[3], m[6]};
    float c1[3] = {m[1], m[4], m[7]};
    float c2[3] = {m[2], m[5], m[8]};
    auto len = [](const float v[3]) { return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); };
    auto d = [](const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
    const float eps = 2e-3f;
    if (std::fabs(len(c0) - 1.f) > eps || std::fabs(len(c1) - 1.f) > eps ||
        std::fabs(len(c2) - 1.f) > eps) return false;
    if (std::fabs(d(c0, c1)) > eps || std::fabs(d(c0, c2)) > eps ||
        std::fabs(d(c1, c2)) > eps) return false;
    const float det =
        m[0]*(m[4]*m[8]-m[5]*m[7]) - m[1]*(m[3]*m[8]-m[5]*m[6]) + m[2]*(m[3]*m[7]-m[4]*m[6]);
    return std::fabs(det - 1.f) < 2e-2f;
}

bool VecOk(const float v[3], float limit) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(v[i]) || std::fabs(v[i]) > limit) return false;
    return true;
}

// The exact block shape of ARCHITECTURE 4.0: rot36 + pos12 + lin12 + ang12.
// The intra-block deltas are part of the CHECK, so whatever we adopt as
// MotionBlock satisfies Position == +0x24 etc. — mover::LayoutSane's rule,
// by construction rather than by copying a number.
bool MotionBlockAt(uint32_t pid, uintptr_t base) {
    float rot[9];
    if (!F32s(pid, base, rot, 9) || !Orthonormal3x3(rot)) return false;
    float pos[3], lin[3], ang[3];
    if (!F32s(pid, base + 0x24, pos, 3) || !VecOk(pos, 1e7f)) return false;
    if (!F32s(pid, base + 0x30, lin, 3) || !VecOk(lin, 1e4f)) return false;
    if (!F32s(pid, base + 0x3c, ang, 3) || !VecOk(ang, 1e4f)) return false;
    return true;
}

// Block A + stride inside a primitive: first single-block hits, then the
// pair whose distance is a block-sized stride.
bool FindMotionPair(uint32_t pid, uintptr_t prim, uintptr_t& a, uintptr_t& stride) {
    a = 0; stride = 0;
    std::vector<uintptr_t> singles;
    for (uintptr_t s = 0; s + 0x48 <= 0x400 && singles.size() < 8; s += 4)
        if (MotionBlockAt(pid, prim + s)) singles.push_back(s);
    if (singles.empty()) return false;
    for (size_t i = 0; i + 1 < singles.size(); ++i) {
        const uintptr_t cand = singles[i + 1] - singles[i];
        if (cand < 0x28 || cand > 0x80) continue;
        a = singles[i];
        stride = cand;
        return true;
    }
    // A alone (no twin within 0x400): refuse — one matrix is far too common
    // to adopt without the twin that makes it a motion pair.
    return false;
}

// --- instance children layout ---------------------------------------------

struct ChildLayout {
    uintptr_t container_off = 0;  // instance -> container ptr (ChildrenStart)
    uintptr_t array_off = 0;      // container -> first entry (ChildrenArray)
    uintptr_t end_off = 0;        // container -> end ptr     (ChildrenEnd)
    uintptr_t stride = 0;         // 8 or 16
    int score = 0;
};

bool ValidateChildren(uint32_t pid, uintptr_t inst, uintptr_t container_off,
                      uintptr_t array_off, uintptr_t end_off, uintptr_t stride,
                      uintptr_t first_entries[8], int* n_out) {
    uintptr_t C = 0;
    if (!RV(pid, inst + container_off, C) || !PlausibleHeap(C)) return false;
    uintptr_t A = 0, B = 0;
    if (!RV(pid, C + array_off, A) || !RV(pid, C + end_off, B)) return false;
    if (!PlausibleHeap(A) || !PlausibleHeap(B) || B <= A) return false;
    const uintptr_t bytes = B - A;
    if (stride != 16 && stride != 8) return false;
    if (bytes % stride) return false;
    const uintptr_t n = bytes / stride;
    if (n == 0 || n > 200000) return false;
    // Entries: first qword is the child pointer (memory.cpp's shape).
    int good = 0;
    const uintptr_t probe = n < 6 ? n : 6;
    if (first_entries) for (int i = 0; i < 8; ++i) first_entries[i] = 0;
    for (uintptr_t i = 0; i < probe; ++i) {
        uintptr_t c = 0;
        if (!RV(pid, A + i * stride, c)) return false;
        if (!c) continue;
        if (!PlausibleHeap(c)) return false;
        if (!RttiToken(pid, c).empty()) {
            if (first_entries) first_entries[good] = c;
            ++good;
        }
    }
    if (good == 0) return false;
    if (n_out) *n_out = good;
    return true;
}

// Collect every container candidate, best score first: distinct RTTI tokens
// among the first entries (a service list is diverse; a utility vector is not).
bool FindChildren(uint32_t pid, uintptr_t inst, ChildLayout& out,
                  std::vector<ChildLayout>* all = nullptr) {
    if (!inst) return false;
    struct Cand : ChildLayout { uintptr_t sample[8]; };
    std::vector<Cand> cands;
    static const uintptr_t kPairs[][2] = {{0, 8}, {8, 16}, {16, 24}, {0, 16}};
    static const uintptr_t kStrides[] = {16, 8};
    for (uintptr_t off = 0; off + 8 <= 0x900; off += 8) {
        uintptr_t probeC = 0;
        if (!RV(pid, inst + off, probeC) || !PlausibleHeap(probeC)) continue;
        for (auto& pair : kPairs) {
            for (uintptr_t stride : kStrides) {
                uintptr_t sample[8] = {};
                int ngood = 0;
                if (!ValidateChildren(pid, inst, off, pair[0], pair[1], stride,
                                      sample, &ngood)) continue;
                Cand c{};
                c.container_off = off;
                c.array_off = pair[0];
                c.end_off = pair[1];
                c.stride = stride;
                std::string toks[6];
                int ntok = 0;
                for (int i = 0; i < 8 && i < ngood + 4; ++i) {
                    if (!sample[i]) break;
                    std::string t = RttiToken(pid, sample[i]);
                    if (t.empty()) continue;
                    bool dup = false;
                    for (int j = 0; j < ntok; ++j) if (toks[j] == t) dup = true;
                    if (!dup && ntok < 6) toks[ntok++] = t;
                }
                c.score = ntok * 10;
                memcpy(c.sample, sample, sizeof(sample));
                cands.push_back(c);
            }
        }
    }
    if (cands.empty()) return false;
    std::stable_sort(cands.begin(), cands.end(),
                     [](const Cand& a, const Cand& b) { return a.score > b.score; });
    out = cands.front();
    if (all) {
        size_t n = std::min<size_t>(cands.size(), 6);
        for (size_t i = 0; i < n; ++i) all->push_back(cands[i]);
    }
    return true;
}

std::vector<uintptr_t> ReadChildrenAddrs(uint32_t pid, uintptr_t inst,
                                         const ChildLayout& L, size_t cap = 4096) {
    std::vector<uintptr_t> out;
    uintptr_t C = 0, A = 0, B = 0;
    if (!RV(pid, inst + L.container_off, C)) return out;
    if (!RV(pid, C + L.array_off, A) || !RV(pid, C + L.end_off, B)) return out;
    if (!PlausibleHeap(A) || !PlausibleHeap(B) || B <= A) return out;
    uintptr_t n = (B - A) / L.stride;
    if (n > cap) n = cap;
    out.reserve(n);
    for (uintptr_t i = 0; i < n; ++i) {
        uintptr_t c = 0;
        if (!RV(pid, A + i * L.stride, c)) break;
        if (c && PlausibleHeap(c)) out.push_back(c);
    }
    return out;
}

// --- string objects --------------------------------------------------------

enum StrLayout { kStrUnknown = 0, kStrHeap = 1, kStrInline = 2 };
std::atomic<int> g_str_hint{kStrUnknown};
std::atomic<int> g_str_inline_len_field{-1}; // 8 / 0x10 / 23
// pid the decode seam reads through (published by SetDecodePid)
std::atomic<uint32_t> mem_pid_for_decode{0};

uint32_t u32_at(const unsigned char* raw, int off) {
    uint32_t v; memcpy(&v, raw + off, 4); return v;
}

// Decode a std::string-shaped object WITHOUT assumptions: heap forms first
// (libc++ long and libstdc++ both keep ptr@0/len@8), the MSVC form
// (SSO buf@0, len@0x10, cap@0x18), then inline forms validated by the
// printable rule. The layout hint recorded during discovery is tried first.
std::string DecodeStringObj(uint32_t pid, uintptr_t s) {
    if (!s) return {};
    unsigned char raw[32];
    if (!RD(pid, s, raw, sizeof(raw))) return {};
    auto u64 = [&](int off) { uint64_t v; memcpy(&v, raw + off, 8); return v; };
    auto u32 = [&](int off) { uint32_t v; memcpy(&v, raw + off, 4); return v; };

    auto read_heap = [&](uintptr_t p, uintptr_t len) -> std::string {
        if (!PlausibleHeap(p) || len == 0 || len > 4096) return {};
        std::string out;
        out.resize(len);
        if (!RD(pid, p, &out[0], len)) return {};
        return Printable(out) ? out : std::string{};
    };

    if (g_str_hint.load(std::memory_order_relaxed) == kStrHeap) {
        std::string r = read_heap(u64(0), u64(8));
        if (!r.empty()) return r;
    }
    // heap: ptr@0 len@8
    {
        std::string r = read_heap(u64(0), u64(8));
        if (!r.empty()) return r;
    }
    // MSVC-style: ptr@0 size@0x10 cap@0x18
    {
        const uint64_t cap = u32(0x18), sz = u32(0x10);
        if (cap >= 16 && sz > 0 && sz < cap && sz <= 4096) {
            std::string r = read_heap(u64(0), sz);
            if (!r.empty()) return r;
        }
    }
    // inline: data@0 with a plausible length field
    auto inline_try = [&](int len_off, bool shift8) -> std::string {
        uint64_t len = 0;
        if (len_off == 23) len = raw[23] >> 1;
        else if (len_off == 0x10) len = u32(0x10);
        else len = shift8 ? (uint64_t)(raw[len_off] >> 1) : u64(len_off);
        if (len == 0 || len > 23) return {};
        std::string out((const char*)raw, (size_t)len);
        return Printable(out) ? out : std::string{};
    };
    if (g_str_hint.load(std::memory_order_relaxed) == kStrInline) {
        const int f = g_str_inline_len_field.load(std::memory_order_relaxed);
        if (f == 8 || f == 0x10 || f == 23) {
            std::string r = inline_try(f, f == 23);
            if (!r.empty()) return r;
        }
    }
    const int fields[] = {8, 0x10, 23};
    for (int f : fields) {
        std::string r = inline_try(f, f == 23);
        if (!r.empty()) return r;
    }
    return {};
}

// Discovery-time: does the string object at `s` hold exactly `want`? Records
// HOW (layout hint) so later decodes of unknown strings reuse it.
bool StringHolds(uint32_t pid, uintptr_t s, const std::string& want) {
    if (!s || want.empty() || want.size() > 4096) return false;
    unsigned char raw[32];
    if (!RD(pid, s, raw, sizeof(raw))) return false;
    auto u64 = [&](int off) { uint64_t v; memcpy(&v, raw + off, 8); return v; };
    if (want.size() <= 23 && memcmp(raw, want.data(), want.size()) == 0) {
        if (u64(8) == want.size()) {
            g_str_hint = kStrInline; g_str_inline_len_field = 8;
        } else if (raw[23] == want.size() * 2) {
            g_str_hint = kStrInline; g_str_inline_len_field = 23;
        } else if ((u32_at(raw, 0x10)) == want.size()) {
            g_str_hint = kStrInline; g_str_inline_len_field = 0x10;
        } else {
            g_str_hint = kStrInline; g_str_inline_len_field = 8;
        }
        return true;
    }
    const uintptr_t p = u64(0);
    if (PlausibleHeap(p)) {
        std::string out;
        out.resize(want.size());
        if (RD(pid, p, &out[0], want.size()) && out == want) {
            g_str_hint = kStrHeap;
            g_str_inline_len_field = -1;
            return true;
        }
    }
    return false;
}

// --- image (the lib's own regions) -----------------------------------------

struct Image {
    uint32_t pid = 0;
    uintptr_t base = 0;
    std::string path;
    std::vector<MapRegion> all;
    std::vector<MapRegion> exe;   // r-xp of the image
    std::vector<MapRegion> ro;    // r-- of the image (incl. relro)
    std::vector<MapRegion> rw;    // rw- file-backed + contiguous anon in span
    uintptr_t span_lo = 0, span_hi = 0;
};

bool BuildImage(uint32_t pid, uintptr_t base, Image& img) {
    img = Image{};
    img.pid = pid;
    img.base = base;
    img.all = MapRegions(pid);
    if (img.all.empty() || !base) return false;
    for (auto& r : img.all) {
        if (r.offset == 0 && r.start <= base && base < r.end &&
            !r.perms.empty() && r.perms[0] == 'r') {
            img.path = r.path;
            break;
        }
    }
    if (img.path.empty()) return false;
    img.span_lo = UINTPTR_MAX;
    img.span_hi = 0;
    for (auto& r : img.all) {
        if (!r.path.empty() && r.path == img.path) {
            img.span_lo = std::min(img.span_lo, r.start);
            img.span_hi = std::max(img.span_hi, r.end);
        }
    }
    if (img.span_lo == UINTPTR_MAX) return false;
    // Extend the span over contiguous UNNAMED mappings: an ELF's bss is a
    // path-less anon VMAs sitting right after the file-backed rw segment and
    // can hold globals (that is where a singleton pointer often lives). The
    // kernel's "[heap]" / "[stack]" labels are paths, so they never join.
    bool extended = true;
    while (extended) {
        extended = false;
        for (auto& r : img.all) {
            if (!r.path.empty()) continue;
            if (r.start >= img.span_lo && r.start <= img.span_hi) {
                if (r.end > img.span_hi) { img.span_hi = r.end; extended = true; }
            }
        }
    }
    for (auto& r : img.all) {
        const bool file = !r.path.empty() && r.path == img.path;
        const bool anon_in_span = r.path.empty() && r.start >= img.span_lo &&
                                  r.end <= img.span_hi;
        if (!file && !anon_in_span) continue;
        if (r.perms.rfind("r-x", 0) == 0) img.exe.push_back(r);
        else if (r.perms.rfind("r--", 0) == 0) img.ro.push_back(r);
        else if (r.perms.rfind("rw-", 0) == 0) img.rw.push_back(r);
    }
    return !img.exe.empty() && !img.ro.empty();
}

} // namespace

// ---------------------------------------------------------------- MapRegions

std::vector<MapRegion> MapRegions(uint32_t pid, bool* denied_out) {
    std::vector<MapRegion> out;
    if (denied_out) *denied_out = false;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/maps", pid);
    std::ifstream f(path);
    if (!f) {
        if (denied_out && (errno == EACCES || errno == EPERM)) *denied_out = true;
        return out;
    }
    std::string line;
    while (std::getline(f, line)) {
        uintptr_t start = 0, end = 0;
        std::string perms, mpath;
        if (!ParseMapsLine(line, start, end, perms, mpath)) continue;
        MapRegion r;
        r.start = start;
        r.end = end;
        std::istringstream tokens(line);
        std::string range, permissions, offset;
        tokens >> range >> permissions >> offset;
        try { r.offset = std::stoull(offset, nullptr, 16); } catch (...) { continue; }
        r.perms = perms;
        r.path = mpath;
        out.push_back(std::move(r));
    }
    return out;
}

// ---------------------------------------------------------------- FindSoberPid

namespace {
uintptr_t VerifiedBase(uint32_t pid, const std::vector<MapRegion>& regions) {
    uintptr_t found = 0;
    for (const auto& region : regions) {
        if (region.offset || region.perms.empty() || region.perms[0] != 'r' ||
            region.path.empty() || region.path[0] != '/') continue;
        auto image = linux_elf::Inspect(pid, region.start);
        if (!image.valid || image.soname != "libroblox.so") continue;
        bool executable = false;
        for (const auto& code : image.executable_ranges)
            for (const auto& mapping : regions)
                if (mapping.perms.find('x') != std::string::npos &&
                    mapping.start < code.second && mapping.end > code.first)
                    executable = true;
        if (!executable) continue;
        if (found && found != image.bias) return 0; // Ambiguous images: fail closed.
        found = image.bias;
    }
    return found;
}
} // namespace

uint32_t FindSoberPid() {
    DIR* directory = opendir("/proc");
    if (!directory) return 0;
    uint32_t found = 0;
    while (auto* entry = readdir(directory)) {
        char* end = nullptr;
        unsigned long value = std::strtoul(entry->d_name, &end, 10);
        if (*end || !value || value > UINT32_MAX) continue;
        const auto pid = static_cast<uint32_t>(value);
        struct stat st{};
        const auto root = "/proc/" + std::to_string(pid);
        if (stat(root.c_str(), &st) || st.st_uid != getuid()) continue;
        auto start = ProcessStartTicks(pid);
        auto regions = MapRegions(pid);
        if (!VerifiedBase(pid, regions) || start.empty() || start != ProcessStartTicks(pid)) continue;
        if (found && found != pid) { found = 0; break; }
        found = pid;
    }
    closedir(directory);
    return found;
}

uintptr_t LibRobloxBase(uint32_t pid, bool* denied_out) {
    auto start = ProcessStartTicks(pid);
    auto regions = MapRegions(pid, denied_out);
    auto base = VerifiedBase(pid, regions);
    return !start.empty() && start == ProcessStartTicks(pid) ? base : 0;
}

std::string ProcessStartTicks(uint32_t pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string line; std::getline(file, line);
    auto close = line.rfind(')');
    if (close == std::string::npos) return {};
    std::istringstream fields(line.substr(close + 1));
    std::string value;
    for (int field = 3; field <= 22; ++field) if (!(fields >> value)) return {};
    return value;
}

// ---------------------------------------------------------------- InReadableRegion

bool InReadableRegion(uintptr_t addr, const std::vector<MapRegion>& regions) {
    for (auto& r : regions) {
        if (addr >= r.start && addr < r.end)
            return !r.perms.empty() && r.perms[0] == 'r';
    }
    return false;
}

// ---------------------------------------------------------------- discovery

namespace {

// ---------- anchor 1: VisualEngine singleton via static RTTI ---------------
// 1. mangled RTTI strings naming VisualEngine in r--
// 2. their typeinfo objects (qword == string at typeinfo+8)
// 3. their vtables (offset-to-top 0, typeinfo at +8, vptr = +16 slot addr)
// 4. the image's data scanned for heap pointers whose object starts at such
//    a vptr
// 5. each survivor validated exactly the way orchestrator validates its
//    hardcoded read: member scan for a DataModel-classed pointer, one hop
//    down again, then a children container that reads. One survivor or fail.
struct Anchor {
    uintptr_t dataModel = 0;
    uintptr_t visualEngine = 0;
    uintptr_t visualEngineGlobal = 0;
    uintptr_t fakeDmOff = 0;
    uintptr_t realDmOff = 0;
    uintptr_t taskScheduler = 0;
    uintptr_t renderJob = 0;
};

// Read whole regions into memory once (rodata can be large; one pass).
bool LoadChunks(uint32_t pid, const std::vector<MapRegion>& regs,
                std::vector<std::pair<uintptr_t, std::vector<unsigned char>>>& out,
                size_t max_total = 384u << 20) {
    out.clear();
    size_t total = 0;
    for (auto& r : regs) {
        const size_t n = (size_t)(r.end - r.start);
        if (!n || total + n > max_total) continue;
        std::vector<unsigned char> buf(n);
        if (!RD(pid, r.start, buf.data(), n)) continue;
        out.emplace_back(r.start, std::move(buf));
        total += n;
    }
    return !out.empty();
}

bool VeAnchor(const Image& img, Anchor& out, std::string& diag) {
    // (1) RTTI strings: find "VisualEngine" in the loaded ro chunks, then the
    // nearest preceding "_ZTS".
    std::vector<std::pair<uintptr_t, std::vector<unsigned char>>> ro;
    if (!LoadChunks(img.pid, img.ro, ro)) {
        diag = "anchor: r-- unreadable";
        return false;
    }
    std::vector<uintptr_t> strings;
    {
        const std::string needle = "VisualEngine";
        for (auto& [base, buf] : ro) {
            for (size_t i = 0; i + needle.size() <= buf.size() && strings.size() < 8;) {
                const void* hit = memmem(buf.data() + i, buf.size() - i,
                                         needle.data(), needle.size());
                if (!hit) break;
                const size_t off = (const unsigned char*)hit - buf.data();
                size_t z = off > 700 ? off - 700 : 0;
                size_t found = std::string::npos;
                for (size_t k = z; k + 4 <= off + needle.size(); ++k) {
                    if (!memcmp(buf.data() + k, "_ZTS", 4)) { found = k; break; }
                }
                i = off + 1;
                if (found == std::string::npos) continue;
                std::string m;
                if (!ReadCString(img.pid, base + found, m, 256)) continue;
                if (m.rfind("_ZTS", 0) || m.find("VisualEngine") == std::string::npos) continue;
                uintptr_t addr = base + found;
                if (std::find(strings.begin(), strings.end(), addr) == strings.end())
                    strings.push_back(addr);
            }
            if (strings.size() >= 8) break;
        }
    }
    if (strings.empty()) { diag = "anchor: no *VisualEngine* RTTI string in r--"; return false; }

    // (2)+(3) typeinfos -> vtables -> vptr values (in-memory scans)
    std::vector<uintptr_t> vptrs;
    auto read_qword = [&](uintptr_t addr, uintptr_t& v) -> bool {
        for (auto& [base, b] : ro) {
            if (addr < base || addr + 8 > base + b.size()) continue;
            memcpy(&v, b.data() + (addr - base), 8);
            return true;
        }
        return false;
    };
    for (uintptr_t s : strings) {
        for (auto& [base, buf] : ro) {
            for (size_t i = 8; i + 24 <= buf.size(); i += 8) {
                uintptr_t q;
                memcpy(&q, buf.data() + i, 8);
                if (q != s) continue;
                const uintptr_t ti = base + i - 8; // the qword sits at typeinfo+8
                // find qword == ti elsewhere (vtable typeinfo slot at +8)
                for (auto& [base2, buf2] : ro) {
                    for (size_t j = 8; j + 24 <= buf2.size(); j += 8) {
                        uintptr_t t;
                        memcpy(&t, buf2.data() + j, 8);
                        if (t != ti) continue;
                        uintptr_t zero;
                        memcpy(&zero, buf2.data() + j - 8, 8);
                        if (zero != 0) continue; // offset-to-top
                        const uintptr_t vptr = base2 + j + 8;
                        if (std::find(vptrs.begin(), vptrs.end(), vptr) == vptrs.end())
                            vptrs.push_back(vptr);
                    }
                }
            }
        }
    }
    (void)read_qword;
    if (vptrs.empty()) {
        diag = "anchor: VisualEngine RTTI strings found, no vtable shaped [0|typeinfo|fn...]";
        return false;
    }

    // (4) data scan: (global address, heap object pointer) pairs
    struct GV { uintptr_t global, obj; };
    std::vector<GV> gv;
    gv.reserve(1 << 20);
    {
        std::vector<unsigned char> buf;
        for (auto& r : img.rw) {
            for (uintptr_t at = r.start; at < r.end;) {
                const size_t n = (size_t)std::min<uintptr_t>(1u << 20, r.end - at);
                buf.resize(n);
                if (!RD(img.pid, at, buf.data(), n)) { at += n; continue; }
                for (size_t i = 0; i + 8 <= n; i += 8) {
                    uintptr_t v;
                    memcpy(&v, buf.data() + i, 8);
                    if (!PlausibleHeap(v)) continue;
                    if (v >= img.span_lo && v < img.span_hi) continue; // into image
                    gv.push_back({at + i, v});
                    if (gv.size() >= 4000000ull) break;
                }
                if (gv.size() >= 4000000ull) break;
                at += n;
            }
            if (gv.size() >= 4000000ull) break;
        }
    }
    if (gv.empty()) { diag = "anchor: no heap pointers in image data"; return false; }

    std::vector<uintptr_t> vals, readback;
    vals.reserve(gv.size());
    for (auto& g : gv) vals.push_back(g.obj);
    RVectored(img.pid, vals, readback);

    // hits: unique object addresses whose vtable slot matches; first storage
    // address recorded as the global
    std::vector<uintptr_t> hits;
    std::vector<uintptr_t> hit_globals;
    for (size_t i = 0; i < gv.size() && i < readback.size(); ++i) {
        if (readback[i] == 0) continue;
        if (std::find(vptrs.begin(), vptrs.end(), readback[i]) == vptrs.end()) continue;
        if (std::find(hits.begin(), hits.end(), gv[i].obj) != hits.end()) continue;
        hits.push_back(gv[i].obj);
        hit_globals.push_back(gv[i].global);
    }
    if (hits.empty()) {
        diag = "anchor: no object matches the VisualEngine vtable (" +
               std::to_string(vptrs.size()) + " vptr(s), " +
               std::to_string(gv.size()) + " pointer candidate(s))";
        return false;
    }

    // (5) chain validation
    int passing = 0;
    for (size_t k = 0; k < hits.size(); ++k) {
        const uintptr_t ve = hits[k];
        uintptr_t fdm_off = 0, fdm_val = 0;
        if (FindMembersByToken(img.pid, ve, "DataModel", &fdm_off, &fdm_val) < 1) continue;
        uintptr_t rdm_off = 0, rdm_val = 0;
        if (FindMembersByToken(img.pid, fdm_val, "DataModel", &rdm_off, &rdm_val) < 1) continue;
        ChildLayout L;
        if (!FindChildren(img.pid, rdm_val, L)) continue;
        out.dataModel = rdm_val;
        out.visualEngine = ve;
        out.visualEngineGlobal = hit_globals[k];
        out.fakeDmOff = fdm_off;
        out.realDmOff = rdm_off;
        ++passing;
    }
    if (passing == 0) {
        diag = "anchor: VE candidate(s) found but the DataModel chain did not validate";
        return false;
    }
    if (passing > 1) {
        diag = "anchor: " + std::to_string(passing) +
               " VE candidates passed — ambiguous, failing closed";
        return false;
    }
    diag = "anchor: VisualEngine RTTI chain ok";
    return true;
}

// ---------- anchor 2: TaskScheduler lea signature (LINUX_PORT_PLAN 6.2) -----
bool TsAnchor(const Image& img, Anchor& out, std::string& diag) {
    // 48 8D 0D ?? ?? ?? ?? 48 8B D7 E8 — lea rcx,[rip+disp]; mov rdx,rdi; call
    bool pattern_seen = false;
    for (auto& r : img.exe) {
        const size_t n = (size_t)(r.end - r.start);
        if (n < 16) continue;
        std::vector<unsigned char> buf(n);
        if (!RD(img.pid, r.start, buf.data(), n)) continue;
        for (size_t i = 0; i + 16 <= n; ++i) {
            if (buf[i] != 0x48 || buf[i + 1] != 0x8D || buf[i + 2] != 0x0D) continue;
            if (buf[i + 7] != 0x48 || buf[i + 8] != 0x8B || buf[i + 9] != 0xD7 ||
                buf[i + 10] != 0xE8) continue;
            pattern_seen = true;
            int32_t disp;
            memcpy(&disp, buf.data() + i + 3, 4);
            if (disp < -8 * 1024 * 1024 || disp > 16 * 1024 * 1024) continue;
            const uintptr_t hit = r.start + i;
            const uintptr_t global = hit + 7 + (intptr_t)disp;
            uintptr_t ts = 0;
            if (!RV(img.pid, global, ts) || !PlausibleHeap(ts)) continue;
            if (RttiToken(img.pid, ts).find("TaskScheduler") == std::string::npos) continue;
            // job vector: [first,end) pair of 8-byte job pointers
            for (uintptr_t off = 0; off + 16 <= 0x400; off += 8) {
                uintptr_t A = 0, B = 0;
                if (!RV(img.pid, ts + off, A)) continue;
                if (!RV(img.pid, ts + off + 8, B)) continue;
                if (!PlausibleHeap(A) || !PlausibleHeap(B) || B <= A) continue;
                const uintptr_t bytes = B - A;
                if (bytes % 8 || bytes / 8 < 4 || bytes / 8 > 8192) continue;
                int jobs = 0;
                uintptr_t renderJob = 0;
                const uintptr_t probe = bytes / 8 < 24 ? bytes / 8 : 24;
                for (uintptr_t j = 0; j < probe; ++j) {
                    uintptr_t job = 0;
                    if (!RV(img.pid, A + j * 8, job) || !PlausibleHeap(job)) continue;
                    const std::string jt = RttiToken(img.pid, job);
                    if (jt.empty()) continue;
                    ++jobs;
                    if (jt.find("Render") != std::string::npos && !renderJob)
                        renderJob = job;
                }
                if (jobs < 2 || !renderJob) continue;
                // RenderJob -> DataModel: first any DataModel-classed member
                // that already reads as the real DM (has children), else one
                // more member hop
                for (uintptr_t o = 0; o + 8 <= 0x1500; o += 8) {
                    uintptr_t p = 0;
                    if (!RV(img.pid, renderJob + o, p) || !PlausibleHeap(p)) continue;
                    if (RttiToken(img.pid, p).find("DataModel") == std::string::npos) continue;
                    ChildLayout L;
                    if (FindChildren(img.pid, p, L)) {
                        out.dataModel = p;
                        out.taskScheduler = ts;
                        out.renderJob = renderJob;
                        diag = "anchor: TaskScheduler signature + job walk ok";
                        return true;
                    }
                    uintptr_t o2 = 0, v2 = 0;
                    if (FindMembersByToken(img.pid, p, "DataModel", &o2, &v2) >= 1 &&
                        FindChildren(img.pid, v2, L)) {
                        out.dataModel = v2;
                        out.taskScheduler = ts;
                        out.renderJob = renderJob;
                        diag = "anchor: TaskScheduler signature + two-hop DataModel ok";
                        return true;
                    }
                }
            }
            diag = "anchor: TaskScheduler lea matched but the job walk found no DataModel";
            return false;
        }
    }
    diag = std::string("anchor: TaskScheduler lea signature ") +
           (pattern_seen ? "seen but never validated" : "not found") +
           " (both anchors failed)";
    return false;
}

// ---------- instance layout -------------------------------------------------

struct InstLayout {
    ChildLayout children;
    uintptr_t classDescriptor = 0, className = 0;
    uintptr_t nameContainer = 0, name = 0;
    uintptr_t parent = 0;
};

// ClassDescriptor on a known instance: a member pointing at a descriptor
// whose +className string object holds `known_class`.
bool FindDescriptor(uint32_t pid, uintptr_t inst, const std::string& known_class,
                    uintptr_t& desc_off, uintptr_t& name_off) {
    for (uintptr_t off = 0; off + 8 <= 0x600; off += 8) {
        uintptr_t D = 0;
        if (!RV(pid, inst + off, D) || !PlausibleHeap(D)) continue;
        for (uintptr_t inner = 0; inner + 8 <= 0x40; inner += 8) {
            uintptr_t S = 0;
            if (!RV(pid, D + inner, S) || !PlausibleHeap(S)) continue;
            if (StringHolds(pid, S, known_class)) {
                desc_off = off;
                name_off = inner;
                return true;
            }
        }
    }
    return false;
}

// NameContainer: a member whose container holds `expected`
// (inline or heap). Records the string layout hint on success.
bool FindNameContainer(uint32_t pid, uintptr_t inst, const std::string& expected,
                       uintptr_t& cont_off, uintptr_t& name_off) {
    for (uintptr_t off = 0; off + 8 <= 0x600; off += 8) {
        uintptr_t C = 0;
        if (!RV(pid, inst + off, C) || !PlausibleHeap(C)) continue;
        for (uintptr_t inner : {uintptr_t(0), uintptr_t(8), uintptr_t(0x10)}) {
            if (StringHolds(pid, C + inner, expected)) {
                cont_off = off;
                name_off = inner;
                return true;
            }
        }
    }
    return false;
}

std::string ClassOf(uint32_t pid, uintptr_t obj, const InstLayout& L) {
    if (L.classDescriptor) {
        uintptr_t D = 0, S = 0;
        if (RV(pid, obj + L.classDescriptor, D) && PlausibleHeap(D) &&
            RV(pid, D + L.className, S)) {
            std::string s = DecodeStringObj(pid, S);
            if (!s.empty()) return s;
        }
    }
    return RttiToken(pid, obj);
}

std::string NameOf(uint32_t pid, uintptr_t obj, const InstLayout& L) {
    if (!L.nameContainer) return {};
    uintptr_t C = 0;
    if (!RV(pid, obj + L.nameContainer, C) || !PlausibleHeap(C)) return {};
    return DecodeStringObj(pid, C + L.name);
}

uintptr_t FindByName(uint32_t pid, const std::vector<uintptr_t>& kids,
                     const InstLayout& L, const std::string& want) {
    for (uintptr_t k : kids)
        if (ClassOf(pid, k, L) == want || NameOf(pid, k, L) == want) return k;
    return 0;
}

bool DiscoverInstanceLayout(uint32_t pid, uintptr_t rdm, InstLayout& L,
                            std::string& diag) {
    std::vector<ChildLayout> cands;
    if (!FindChildren(pid, rdm, L.children, &cands)) {
        diag = "layout: DataModel has no children vector that validates";
        return false;
    }
    const std::string rtti_cls = RttiToken(pid, rdm);
    if (rtti_cls.empty()) {
        diag = "layout: DataModel RTTI unreadable — cannot ground-truth discovery";
        return false;
    }
    // ClassDescriptor: member -> descriptor -> string holding the RTTI class
    // name; verified independently on two children (descriptor-RTTI agreement)
    bool desc_ok = false;
    for (auto& c : cands) {
        uintptr_t desc = 0, cname = 0;
        if (!FindDescriptor(pid, rdm, rtti_cls, desc, cname)) continue;
        auto kids = ReadChildrenAddrs(pid, rdm, c);
        int verified = 0;
        for (uintptr_t k : kids) {
            const std::string rt = RttiToken(pid, k);
            if (rt.empty()) continue;
            uintptr_t d2 = 0, n2 = 0;
            if (FindDescriptor(pid, k, rt, d2, n2) && d2 == desc && n2 == cname)
                ++verified;
            if (verified >= 2) break;
        }
        if (verified >= 1) {
            L.children = c;
            L.classDescriptor = desc;
            L.className = cname;
            desc_ok = true;
            break;
        }
    }
    if (!desc_ok) {
        diag = "layout: ClassDescriptor not verified (RTTI class names will be used)";
        L.classDescriptor = 0;
        L.className = 0;
        L.children = cands.front();
    }

    // Parent: two children both pointing back at rdm at the same offset
    {
        auto kids = ReadChildrenAddrs(pid, rdm, L.children);
        if (kids.size() >= 2) {
            std::vector<uintptr_t> hit1, hit2;
            for (uintptr_t off = 0; off + 8 <= 0x400; off += 8) {
                uintptr_t v = 0;
                if (RV(pid, kids[0] + off, v) && v == rdm) hit1.push_back(off);
                if (RV(pid, kids[1] + off, v) && v == rdm) hit2.push_back(off);
            }
            for (uintptr_t a : hit1)
                for (uintptr_t b : hit2)
                    if (a == b) { L.parent = a; break; }
        }
    }

    // Name ground truth: the DataModel against known names, else a service
    // child against its own class token (services are named after their class)
    const char* dm_names[] = {"Game", "DataModel", "game"};
    bool name_ok = false;
    for (const char* want : dm_names)
        if (FindNameContainer(pid, rdm, want, L.nameContainer, L.name)) {
            name_ok = true;
            break;
        }
    if (!name_ok) {
        auto kids = ReadChildrenAddrs(pid, rdm, L.children);
        for (uintptr_t k : kids) {
            const std::string rt = RttiToken(pid, k);
            if (rt.empty() || rt.size() > 32) continue;
            if (FindNameContainer(pid, k, rt, L.nameContainer, L.name)) {
                name_ok = true;
                break;
            }
        }
    }
    if (!name_ok) {
        diag = "layout: name container did not decode against any known service name";
        return false;
    }

    char b[64];
    snprintf(b, sizeof(b),
             "layout: children cont+0x%llx arr+%llx end+%llx stride %llu, "
             "descriptor %s, name +%llx/%llx, parent +%llx, dm name \"%s\"",
             (unsigned long long)L.children.container_off,
             (unsigned long long)L.children.array_off,
             (unsigned long long)L.children.end_off,
             (unsigned long long)L.children.stride,
             L.classDescriptor ? "verified" : "none (RTTI names)",
             (unsigned long long)L.nameContainer,
             (unsigned long long)L.name,
             (unsigned long long)L.parent,
             NameOf(pid, rdm, L).c_str());
    diag = b;
    return true;
}

bool MemberPointingAt(uint32_t pid, uintptr_t owner, uintptr_t target,
                      uintptr_t limit, uintptr_t& off_out) {
    if (!owner || !target) return false;
    int hits = 0;
    for (uintptr_t o = 0; o + 8 <= limit; o += 8) {
        uintptr_t v = 0;
        if (!RV(pid, owner + o, v) || v != target) continue;
        off_out = o;
        ++hits;
        if (hits > 1) return false;
    }
    return hits == 1;
}

// ---------- static stage ----------------------------------------------------

struct StaticState {
    bool done = false;
    bool ok = false;
    uint32_t pid = 0;
    uintptr_t base = 0;
    std::string start;
    Anchor anchor;
    InstLayout inst;
    uintptr_t workspace = 0;
    uintptr_t players = 0;
    uintptr_t camera = 0;
    uintptr_t wsCurrentCamera = 0;
    uintptr_t playersLocalPlayer = 0;
    uintptr_t playerModelInstance = 0;
    uintptr_t playerTeam = 0;
    std::string diag;
};
StaticState g_static;

void ResetDiscovery() {
    g_static = StaticState{};
    g_str_hint.store(kStrUnknown, std::memory_order_relaxed);
    g_str_inline_len_field.store(-1, std::memory_order_relaxed);
    mem_pid_for_decode.store(0, std::memory_order_relaxed);
}

bool RunStatic(uint32_t pid, uintptr_t base, ScanResult& res) {
    Image img;
    if (!BuildImage(pid, base, img)) {
        res.diag = "scan: cannot map image regions for pid " + std::to_string(pid);
        return false;
    }
    Anchor anchor;
    std::string diag;
    if (!VeAnchor(img, anchor, diag)) {
        std::string ts_diag;
        if (!TsAnchor(img, anchor, ts_diag)) {
            res.diag = diag + " | " + ts_diag;
            return false;
        }
        diag += " | " + ts_diag;
    }
    InstLayout L;
    std::string layout_diag;
    if (!DiscoverInstanceLayout(pid, anchor.dataModel, L, layout_diag)) {
        res.diag = diag + " | " + layout_diag;
        return false;
    }
    diag += " | " + layout_diag;

    const auto kids = ReadChildrenAddrs(pid, anchor.dataModel, L.children);
    if (kids.empty()) {
        res.diag = diag + " | scan: DataModel children read empty";
        return false;
    }
    const uintptr_t ws = FindByName(pid, kids, L, "Workspace");
    const uintptr_t plrs = FindByName(pid, kids, L, "Players");
    if (!ws) {
        res.diag = diag + " | scan: no Workspace child (first child class \"" +
                   ClassOf(pid, kids[0], L) + "\")";
        return false;
    }

    // Workspace::CurrentCamera: exact "Camera" token first, substring fallback
    uintptr_t cam_off = 0, cam_val = 0;
    int cam_hits = FindMembersByExactToken(pid, ws, "Camera", &cam_off, &cam_val);
    if (cam_hits != 1)
        cam_hits = FindMembersByToken(pid, ws, "Camera", &cam_off, &cam_val);
    const uintptr_t camera = (cam_hits == 1) ? cam_val : 0;

    // Players::LocalPlayer: member pointing at a "Player"-tokened CHILD
    uintptr_t lp_off = 0;
    if (plrs) {
        auto pkids = ReadChildrenAddrs(pid, plrs, L.children);
        for (uintptr_t o = 0; o + 8 <= 0x800 && lp_off == 0; o += 8) {
            uintptr_t v = 0;
            if (!RV(pid, plrs + o, v) || !PlausibleHeap(v)) continue;
            if (std::find(pkids.begin(), pkids.end(), v) == pkids.end()) continue;
            if (RttiToken(pid, v) == "Player") lp_off = o;
        }
        if (!lp_off) {
            uintptr_t off = 0, val = 0;
            if (FindMembersByToken(pid, plrs, "Player", &off, &val) == 1) lp_off = off;
        }
    }
    uintptr_t model_off = 0, team_off = 0;
    uintptr_t any_player = 0;
    if (plrs) {
        auto pkids = ReadChildrenAddrs(pid, plrs, L.children);
        if (!pkids.empty()) any_player = pkids[0];
    }
    if (any_player) {
        uintptr_t off = 0, val = 0;
        if (FindMembersByToken(pid, any_player, "Model", &off, &val) == 1)
            model_off = off;
        off = 0; val = 0;
        // Team: 0 is a valid answer (places without teams) — keep quiet
        if (FindMembersByExactToken(pid, any_player, "Team", &off, &val) == 1)
            team_off = off;
    }

    g_static.done = true;
    g_static.ok = true;
    g_static.pid = pid;
    g_static.base = base;
    g_static.start = ProcessStartTicks(pid);
    g_static.anchor = anchor;
    g_static.inst = L;
    g_static.workspace = ws;
    g_static.players = plrs;
    g_static.camera = camera;
    g_static.wsCurrentCamera = (cam_hits == 1) ? cam_off : 0;
    g_static.playersLocalPlayer = lp_off;
    g_static.playerModelInstance = model_off;
    g_static.playerTeam = team_off;
    g_static.diag = diag;

    res.taskScheduler = anchor.taskScheduler;
    res.renderJob = anchor.renderJob;
    res.dataModel = anchor.dataModel;
    res.visualEngine = anchor.visualEngine;
    res.visualEngineGlobal = anchor.visualEngineGlobal;
    res.workspace = ws;
    res.players = plrs;
    res.camera = camera;
    res.instChildrenContainer = L.children.container_off;
    res.instChildrenArray = L.children.array_off;
    res.instChildrenEnd = L.children.end_off;
    res.instChildrenStride = L.children.stride;
    res.instClassDescriptor = L.classDescriptor;
    res.instClassName = L.className;
    res.instNameContainer = L.nameContainer;
    res.instName = L.name;
    res.instParent = L.parent;
    res.wsCurrentCamera = g_static.wsCurrentCamera;
    res.playersLocalPlayer = g_static.playersLocalPlayer;
    res.playerModelInstance = g_static.playerModelInstance;
    res.playerTeam = g_static.playerTeam;
    res.staticOk = true;
    res.diag = diag;
    return true;
}

// ---------- runtime stage ---------------------------------------------------

bool RunRefine(uint32_t pid, uintptr_t base, ScanResult& res) {
    if (!g_static.ok || g_static.pid != pid || g_static.base != base) {
        res.diag = "refine: static stage not valid for this process";
        return false;
    }
    const InstLayout& L = g_static.inst;
    std::string note;
    char hexbuf[80];

    // -- VisualEngine dimensions (the viewport pair esp's GetCamera reads)
    if (res.visualEngine && !res.visualEngineDims) {
        int best_off = -1;
        float best_area = -1.f;
        int gx = 0, gy = 0, gw = 0, gh = 0;
        const bool have_rect = platform::GameClientRect(gx, gy, gw, gh) && gw > 0 && gh > 0;
        for (uintptr_t off = 0; off + 8 <= 0x1200; off += 8) {
            float wh[2];
            if (!F32s(pid, res.visualEngine + off, wh, 2)) break;
            if (wh[0] != std::floor(wh[0]) || wh[1] != std::floor(wh[1])) continue;
            if (wh[0] < 64 || wh[0] > 16384 || wh[1] < 64 || wh[1] > 16384) continue;
            if (have_rect) {
                const bool match = (std::fabs(wh[0] - (float)gw) <= 2.f &&
                                    std::fabs(wh[1] - (float)gh) <= 2.f);
                if (match) { best_off = (int)off; best_area = wh[0] * wh[1]; break; }
                continue; // rect known: only accept a match
            }
            const float area = wh[0] * wh[1];
            if (area > best_area) { best_area = area; best_off = (int)off; }
        }
        if (best_off >= 0) {
            res.visualEngineDims = (uintptr_t)best_off;
            float wh[2] = {};
            F32s(pid, res.visualEngine + res.visualEngineDims, wh, 2);
            snprintf(hexbuf, sizeof(hexbuf), " ve-dims+0x%x(%dx%d)",
                     best_off, (int)wh[0], (int)wh[1]);
            note += hexbuf;
        } else {
            note += have_rect ? " ve-dims: no pair matching the game rect"
                              : " ve-dims: no plausible viewport pair";
        }
    }

    // -- camera numerics (rotation matrix + position at +0x24, FOV in radians)
    if (g_static.camera && !res.cameraPosition) {
        for (uintptr_t s = 0; s + 0x40 <= 0x400; s += 4) {
            float rot[9];
            if (!F32s(pid, g_static.camera + s, rot, 9)) break;
            if (!Orthonormal3x3(rot)) continue;
            float pos[3];
            if (!F32s(pid, g_static.camera + s + 0x24, pos, 3)) continue;
            if (!VecOk(pos, 1e7f)) continue;
            res.cameraRotation = s;
            res.cameraPosition = s + 0x24;
            snprintf(hexbuf, sizeof(hexbuf), " camera rot/pos @0x%llx/0x%llx",
                     (unsigned long long)s, (unsigned long long)(s + 0x24));
            note += hexbuf;
            break;
        }
        if (!res.cameraPosition)
            note += " camera: no matrix+delta pattern (ESP stays off)";
    }
    if (g_static.camera && !res.cameraFov) {
        const float target = 1.2217305f; // ~70 deg, what esp falls back to
        float best_d = 1e9f;
        int n_cand = 0;
        for (uintptr_t off = 0; off + 4 <= 0x400; off += 4) {
            float v = 0;
            if (!F32s(pid, g_static.camera + off, &v, 1)) break;
            if (!std::isfinite(v) || v < 0.4f || v > 2.4f) continue;
            ++n_cand;
            const float d = std::fabs(v - target);
            if (d < best_d) { best_d = d; res.cameraFov = off; }
        }
        if (res.cameraFov) {
            float v = 0;
            F32s(pid, g_static.camera + res.cameraFov, &v, 1);
            snprintf(hexbuf, sizeof(hexbuf), " camera fov +0x%llx(v=%.3f of %d cand)",
                     (unsigned long long)res.cameraFov, (double)v, n_cand);
            note += hexbuf;
        } else {
            note += " camera fov: no radian-looking float";
        }
    }

    // -- a spawned character: players -> model -> named parts
    uintptr_t model = 0, humanoid = 0, hrp = 0;
    if (g_static.players && g_static.playerModelInstance) {
        auto pkids = ReadChildrenAddrs(pid, g_static.players, L.children);
        for (uintptr_t p : pkids) {
            uintptr_t m = 0;
            if (!RV(pid, p + g_static.playerModelInstance, m) || !PlausibleHeap(m)) continue;
            auto mkids = ReadChildrenAddrs(pid, m, L.children);
            if (mkids.empty()) continue;
            model = m;
            for (uintptr_t c : mkids) {
                const std::string nm = NameOf(pid, c, L);
                const std::string cl = ClassOf(pid, c, L);
                if (nm == "HumanoidRootPart" && !hrp) hrp = c;
                if (cl == "Humanoid" && !humanoid) humanoid = c;
            }
            if (hrp) break;
        }
    }

    // -- BasePart::Primitive + motion pair: measured on the HRP, confirmed on
    //    a second part of the same model (same offsets both times)
    if (hrp && !res.primMotionBlock) {
        for (uintptr_t off = 0; off + 8 <= 0x500; off += 8) {
            uintptr_t prim = 0;
            if (!RV(pid, hrp + off, prim) || !PlausibleHeap(prim)) continue;
            uintptr_t a = 0, stride = 0;
            if (!FindMotionPair(pid, prim, a, stride)) continue;
            int checked = 0;
            bool confirmed = false;
            std::string confirm_note;
            if (model) {
                for (uintptr_t c : ReadChildrenAddrs(pid, model, L.children)) {
                    if (c == hrp) continue;
                    const std::string nm = NameOf(pid, c, L);
                    if (nm != "Head" && nm != "UpperTorso" && nm != "Torso" &&
                        nm != "LowerTorso" && nm != "Right Arm" && nm != "Left Leg")
                        continue;
                    uintptr_t p2 = 0;
                    if (!RV(pid, c + off, p2) || !PlausibleHeap(p2)) continue;
                    uintptr_t a2 = 0, s2 = 0;
                    ++checked;
                    if (FindMotionPair(pid, p2, a2, s2) && a2 == a && s2 == stride) {
                        confirmed = true;
                        break;
                    }
                    if (checked >= 2) break;
                }
                if (!confirmed && checked == 0) confirmed = true; // one part only
                confirm_note = confirmed ? (checked ? " confirmed(x2)" : " single-part")
                                         : " NOT confirmed";
            }
            if (model && !confirmed) continue;
            res.basePartPrimitive = off;
            res.primMotionBlock = a;
            res.primStride = stride;
            snprintf(hexbuf, sizeof(hexbuf), " motion prim+0x%llx block 0x%llx stride 0x%llx%s",
                     (unsigned long long)off, (unsigned long long)a,
                     (unsigned long long)stride, confirm_note.c_str());
            note += hexbuf;
            break;
        }
        if (!res.primMotionBlock)
            note += " motion: no dual-block pair measured yet (needs a spawned character)";
    }

    // -- Humanoid::HumanoidRootPart: exact identity with the named part
    if (humanoid && hrp && !res.humanoidRootPart) {
        uintptr_t off = 0;
        if (MemberPointingAt(pid, humanoid, hrp, 0x800, off)) {
            res.humanoidRootPart = off;
            snprintf(hexbuf, sizeof(hexbuf), " humanoid.hrp +0x%llx",
                     (unsigned long long)off);
            note += hexbuf;
        }
    }

    // -- Humanoid::Health: adjacent floats, health first, positive on every
    //    humanoid seen (optional — never blocks runtimeOk)
    if (g_static.players && !res.humanoidHealth && g_static.playerModelInstance) {
        std::vector<uintptr_t> humanoids;
        for (uintptr_t p : ReadChildrenAddrs(pid, g_static.players, L.children)) {
            uintptr_t m = 0;
            if (!RV(pid, p + g_static.playerModelInstance, m) || !PlausibleHeap(m)) continue;
            for (uintptr_t c : ReadChildrenAddrs(pid, m, L.children))
                if (ClassOf(pid, c, L) == "Humanoid") humanoids.push_back(c);
        }
        if (humanoids.size() >= 2) {
            for (uintptr_t off = 0; off + 4 <= 0x400; off += 4) {
                bool all = true;
                for (uintptr_t h : humanoids) {
                    float pair[2];
                    if (!F32s(pid, h + off, pair, 2) || !std::isfinite(pair[0]) ||
                        !std::isfinite(pair[1])) { all = false; break; }
                    if (!(pair[0] > 0.f && pair[0] <= pair[1] && pair[1] <= 1e6f)) {
                        all = false; break;
                    }
                }
                if (all) {
                    res.humanoidHealth = off;
                    res.humanoidMaxHealth = off + 4;
                    snprintf(hexbuf, sizeof(hexbuf), " humanoid health +0x%llx",
                             (unsigned long long)off);
                    note += hexbuf;
                    break;
                }
            }
        }
    }

    const bool need =
        res.visualEngineDims && res.cameraPosition && res.cameraFov &&
        res.primMotionBlock && res.basePartPrimitive &&
        res.primStride == 0x48; // mover::LayoutSane's documented shape
    if (need && !res.runtimeOk) {
        res.runtimeOk = true;
        note += " | runtime stage COMPLETE";
    }

    res.diag = g_static.diag + (note.empty() ? "" : (" ||" + note));
    return res.runtimeOk;
}

} // namespace

// ---------------------------------------------------------------- Scan / Refine

ScanResult Scan(void* platform_handle, uint32_t pid, uintptr_t libBase) {
    (void)platform_handle;
    ScanResult res;
    if (!pid || !libBase) {
        res.diag = "scan: no verified Linux attachment";
        return res;
    }
    const std::string start = ProcessStartTicks(pid);
    if (start.empty()) {
        res.diag = "scan: process " + std::to_string(pid) + " vanished";
        return res;
    }
    if (g_static.done && g_static.pid == pid && g_static.base == libBase &&
        g_static.start == start) {
        res.taskScheduler = g_static.anchor.taskScheduler;
        res.renderJob = g_static.anchor.renderJob;
        res.dataModel = g_static.anchor.dataModel;
        res.visualEngine = g_static.anchor.visualEngine;
        res.visualEngineGlobal = g_static.anchor.visualEngineGlobal;
        res.workspace = g_static.workspace;
        res.players = g_static.players;
        res.camera = g_static.camera;
        res.instChildrenContainer = g_static.inst.children.container_off;
        res.instChildrenArray = g_static.inst.children.array_off;
        res.instChildrenEnd = g_static.inst.children.end_off;
        res.instChildrenStride = g_static.inst.children.stride;
        res.instClassDescriptor = g_static.inst.classDescriptor;
        res.instClassName = g_static.inst.className;
        res.instNameContainer = g_static.inst.nameContainer;
        res.instName = g_static.inst.name;
        res.instParent = g_static.inst.parent;
        res.wsCurrentCamera = g_static.wsCurrentCamera;
        res.playersLocalPlayer = g_static.playersLocalPlayer;
        res.playerModelInstance = g_static.playerModelInstance;
        res.playerTeam = g_static.playerTeam;
        res.staticOk = g_static.ok;
        res.diag = g_static.diag;
        return res;
    }
    if (g_static.done && (g_static.pid != pid || g_static.base != libBase ||
                          g_static.start != start))
        ResetDiscovery(); // process changed under us (restart / pid reuse)
    const bool ok = RunStatic(pid, libBase, res);
    if (ProcessStartTicks(pid) != start) {
        res = ScanResult{};
        res.diag = "scan: process changed during the scan — refusing the result";
        ResetDiscovery();
        return res;
    }
    if (!ok) g_static = StaticState{};
    return res;
}

bool Refine(void* platform_handle, uint32_t pid, uintptr_t libBase, ScanResult& result) {
    (void)platform_handle;
    if (!pid || !libBase) return false;
    if (!g_static.ok || g_static.pid != pid || g_static.base != libBase) {
        result.diag = "refine: run Scan first";
        return false;
    }
    if (result.runtimeOk) return true;
    return RunRefine(pid, libBase, result);
}

void ResetScanner() { ResetDiscovery(); }

// ---------------------------------------------------------------- decode seam

void SetDecodePid(uint32_t pid) {
    mem_pid_for_decode.store(pid, std::memory_order_relaxed);
}

std::string TryPooledString(void* /*platform_handle*/, uintptr_t container) {
    if (!container) return {};
    const uint32_t pid = mem_pid_for_decode.load(std::memory_order_relaxed);
    if (!pid) return {};
    return DecodeStringObj(pid, container);
}

std::string RttiClassName(void* /*platform_handle*/, uintptr_t object) {
    if (!object) return {};
    const uint32_t pid = mem_pid_for_decode.load(std::memory_order_relaxed);
    if (!pid) return {};
    return RttiToken(pid, object);
}

} // namespace sober

#else // !__linux__

namespace sober {
uint32_t FindSoberPid() { return 0; }
uintptr_t LibRobloxBase(uint32_t, bool*) { return 0; }
std::string ProcessStartTicks(uint32_t) { return {}; }
std::vector<MapRegion> MapRegions(uint32_t, bool*) { return {}; }
ScanResult Scan(void*, uint32_t, uintptr_t) { return {}; }
bool Refine(void*, uint32_t, uintptr_t, ScanResult&) { return false; }
void ResetScanner() {}
void SetDecodePid(uint32_t) {}
std::string TryPooledString(void*, uintptr_t) { return {}; }
std::string RttiClassName(void*, uintptr_t) { return {}; }
bool InReadableRegion(uintptr_t, const std::vector<MapRegion>&) { return false; }
}

#endif
