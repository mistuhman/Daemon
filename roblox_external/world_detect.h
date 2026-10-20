#pragma once

// -----------------------------------------------------------------------------
//  world_detect.h -- pure layout detection for the physics world's primitive
//  array, factored out of cache.cpp so it can be tested against synthetic
//  memory (tools/world_detect_test.cpp) instead of a live game we cannot run.
//
//  The live offset service gives us World::Primitives -- an offset into the
//  physics World object -- but says nothing about the CONTAINER that lives at
//  that offset. No public source documents its layout, so instead of guessing
//  once and hoping, this header tries the two layouts a C++ engine most
//  plausibly uses for "the list of every primitive":
//
//    1. inline vector    [world+P] = data begin, [world+P+8] = data end
//    2. indirect vector  [world+P] = container*, [container+0] = begin, +8 = end
//
//  (entries are 8-byte Primitive* each, like every std::vector<T*>)
//
//  An interpretation is accepted only if ALL of these hold:
//    - the entry count is sane (1 .. 400k) and 8-byte aligned
//    - sampled entries are canonical usermode pointers
//    - at least 3 sampled entries look like real primitives: Position and
//      Size (3 floats each, at the live offsets) are finite and bounded
//    - when DataModel::PrimitiveCount is available as a hint, the count is
//      within 2x of it (both are sampled a moment apart; parts stream in and
//      out, so exact equality is too strict)
//
//  If nothing validates: LAYOUT_NONE, and the wall check falls back to the
//  player-body-only raycast (the world probe on the debug page dumps the raw
//  container so the layout can be pinned by hand from real data).
//
//  A third possible layout -- a dense array of Primitive STRUCTS rather than
//  pointers -- is deliberately not tried: with struct entries the qword at
//  [data] is the first field of a Primitive, not a pointer to one, and the
//  plausibility probe reads the wrong address. That case also lands in
//  LAYOUT_NONE + probe. Same for hash maps and slot arrays.
// -----------------------------------------------------------------------------
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace worlddet {

// Reader: return false when the address is unreadable. ctx carries state (the
// live caller closes over the game handle; the test closes over a buffer).
using Reader = bool (*)(void* ctx, uintptr_t addr, void* out, size_t size);

enum Layout : int {
    LAYOUT_NONE = 0,
    LAYOUT_INLINE_VECTOR = 1,
    LAYOUT_INDIRECT_VECTOR = 2,
};

struct Result {
    Layout layout = LAYOUT_NONE;
    uintptr_t data = 0;   // address of the first entry (a Primitive*)
    size_t count = 0;     // number of 8-byte entries
};

inline bool read_qword(Reader r, void* ctx, uintptr_t addr, uintptr_t& out) {
    return r(ctx, addr, &out, sizeof(out));
}

// Usermode canonical pointer. The game is x64; anything outside
// [0x10000, 0x800000000000) is not a heap object we can follow.
inline bool canonical_ptr(uintptr_t p) {
    return p >= 0x10000ull && p < 0x0000800000000000ull;
}

// A believable primitive: Position and Size are finite, the position is
// inside any world anyone builds (< 1e6 studs), the size is a non-negative
// part extent that fits through a door (< 4096 studs), and at least one axis
// has extent (parts are not points).
inline bool plausible_prim(uintptr_t prim, Reader r, void* ctx,
                           uintptr_t pos_off, uintptr_t size_off) {
    float pos[3] = { 0, 0, 0 }, size[3] = { 0, 0, 0 };
    if (!r(ctx, prim + pos_off, pos, sizeof(pos))) return false;
    if (!r(ctx, prim + size_off, size, sizeof(size))) return false;
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(pos[i]) || std::fabs(pos[i]) > 1e6f) return false;
        if (!std::isfinite(size[i]) || size[i] < 0.0f || size[i] > 4096.0f) return false;
    }
    return size[0] > 0.0f || size[1] > 0.0f || size[2] > 0.0f;
}

// Tries (data, count) as a vector<Primitive*>. Samples up to 6 entries spread
// across the array (the middle of a big list is where a corrupt end pointer
// would send us); every sample must be canonical, and at least 3 (or all of
// them, for tiny lists) must read as plausible primitives.
inline bool validate_entries(uintptr_t data, size_t count, uintptr_t count_hint,
                             Reader r, void* ctx, uintptr_t pos_off, uintptr_t size_off) {
    if (count == 0 || count > 400000) return false;
    if (count_hint != 0) {
        if (count > count_hint * 2 + 64) return false;
        if (count * 2 + 64 < count_hint) return false;
    }
    size_t samples = count < 6 ? count : 6;
    size_t plausible = 0;
    for (size_t i = 0; i < samples; ++i) {
        // entry i of `samples`, spread evenly: 0, n/(s-1), ..., n-1
        size_t idx = (samples == 1) ? 0 : (i * (count - 1)) / (samples - 1);
        uintptr_t entry = 0;
        if (!read_qword(r, ctx, data + idx * 8, entry)) return false;
        if (!canonical_ptr(entry)) return false;
        if (plausible_prim(entry, r, ctx, pos_off, size_off)) ++plausible;
    }
    size_t needed = samples < 3 ? samples : 3;
    return plausible >= needed;
}

inline const char* layout_name(Layout l) {
    switch (l) {
        case LAYOUT_INLINE_VECTOR:   return "inline vector {begin,end}";
        case LAYOUT_INDIRECT_VECTOR: return "indirect vector (*container -> begin,end)";
        default:                     return "not recognised";
    }
}

inline Result detect(uintptr_t world, uintptr_t prims_off, uintptr_t count_hint,
                     Reader r, void* ctx, uintptr_t pos_off, uintptr_t size_off) {
    Result out;
    if (world == 0) return out;

    uintptr_t a0 = 0, a1 = 0;
    if (!read_qword(r, ctx, world + prims_off, a0)) return out;
    // a1 is only needed by candidate 1; the indirect layout makes no
    // promises about the qword after the container pointer, so a failed read
    // just kills candidate 1 instead of the whole detection
    read_qword(r, ctx, world + prims_off + 8, a1);

    // candidate 1: the vector lives at the offset itself
    if (canonical_ptr(a0) && a1 >= a0 && ((a1 - a0) % 8) == 0) {
        size_t n = (size_t)((a1 - a0) / 8);
        if (validate_entries(a0, n, count_hint, r, ctx, pos_off, size_off)) {
            out.layout = LAYOUT_INLINE_VECTOR;
            out.data = a0;
            out.count = n;
            return out;
        }
    }

    // candidate 2: the offset holds a pointer to the vector. This is also
    // what a misread of candidate 1 looks like: [data..end] of a real inline
    // vector reinterpreted as (container, junk) fails the entry checks, so
    // the order of the candidates is safe.
    if (canonical_ptr(a0)) {
        uintptr_t d = 0, e = 0;
        if (read_qword(r, ctx, a0, d) && read_qword(r, ctx, a0 + 8, e) &&
            canonical_ptr(d) && e >= d && ((e - d) % 8) == 0) {
            size_t n = (size_t)((e - d) / 8);
            if (validate_entries(d, n, count_hint, r, ctx, pos_off, size_off)) {
                out.layout = LAYOUT_INDIRECT_VECTOR;
                out.data = d;
                out.count = n;
                return out;
            }
        }
    }

    return out;
}

} // namespace worlddet
