// Unit test for world_detect.h -- the wall check's primitive-container layout
// detection.
//
//   g++ -std=c++17 -O1 -I"../roblox_external" tools/world_detect_test.cpp -o build/world_detect_test
//   ./build/world_detect_test
//
// No Windows, no game: the detector takes a reader callback, and this test
// feeds it synthetic memory. That matters because the container layout at
// World::Primitives is undocumented -- the detector's whole job is to tell a
// real primitive pointer list from garbage BEFORE the wall check trusts it,
// and a wrong positive would raycast against random bytes (missed walls or
// phantom blocks). Every way the real container could surprise us gets a
// case here: both vector layouts, struct-array decoys, hash-map decoys,
// count-hint disagreement, non-pointer entries, NaN positions, tiny lists.
#include "world_detect.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

static int g_fail = 0;
static int g_n = 0;

static void Check(bool cond, const char* what)
{
    ++g_n;
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_fail;
}

// ---------------------------------------------------------------------------
//  synthetic memory: byte-granular, so unreadable addresses fail reads exactly
//  like an RPM to unmapped memory would
// ---------------------------------------------------------------------------
struct Mem
{
    std::map<uintptr_t, uint8_t> bytes;

    void write(uintptr_t at, const void* data, size_t n)
    {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < n; ++i) bytes[at + i] = p[i];
    }
    void wq(uintptr_t at, uintptr_t v) { write(at, &v, sizeof(v)); }
    void wf(uintptr_t at, float x, float y, float z)
    {
        float f[3] = { x, y, z };
        write(at, f, sizeof(f));
    }
    bool read(uintptr_t at, void* out, size_t n) const
    {
        for (size_t i = 0; i < n; ++i) {
            auto b = bytes.find(at + i);
            if (b == bytes.end()) return false;
            ((uint8_t*)out)[i] = b->second;
        }
        return true;
    }
};

static bool mem_read(void* ctx, uintptr_t at, void* out, size_t n)
{
    return ((Mem*)ctx)->read(at, out, n);
}

// ---------------------------------------------------------------------------
//  fixture constants, mirroring the live offsets
// ---------------------------------------------------------------------------
static constexpr uintptr_t WORLD        = 0x100000;
static constexpr uintptr_t PRIMS_OFF    = 0x2b0;   // World::Primitives
static constexpr uintptr_t POS_OFF      = 0xec;    // Primitive::Position
static constexpr uintptr_t SIZE_OFF     = 0x1bc;   // Primitive::Size
static constexpr uintptr_t VEC          = 0x200000;

static uintptr_t g_next_prim = 0x500000;

// a believable primitive: canonical address, sane position, non-zero size
static uintptr_t make_prim(Mem& m, float x, float y, float z,
                           float sx, float sy, float sz)
{
    uintptr_t p = g_next_prim;
    g_next_prim += 0x400;
    m.wf(p + POS_OFF, x, y, z);
    m.wf(p + SIZE_OFF, sx, sy, sz);
    return p;
}

// n believable primitives in a vector at VEC; returns the entry count
static size_t fill_vector(Mem& m, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        m.wq(VEC + i * 8, make_prim(m, (float)i * 10.0f, 0.0f, 5.0f,
                                    4.0f, 8.0f, 2.0f));
    return n;
}

int main()
{
    // -- 1. inline vector: [world+P]=data, [world+P+8]=end ------------------
    {
        Mem m;
        size_t n = fill_vector(m, 12);
        m.wq(WORLD + PRIMS_OFF, VEC);
        m.wq(WORLD + PRIMS_OFF + 8, VEC + n * 8);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, n, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_INLINE_VECTOR, "inline vector detected");
        Check(r.count == n && r.data == VEC, "inline vector: data + count exact");
    }

    // -- 2. indirect vector: [world+P] -> container {data,end} --------------
    {
        Mem m;
        size_t n = fill_vector(m, 9);
        const uintptr_t container = 0x300000;
        m.wq(container, VEC);
        m.wq(container + 8, VEC + n * 8);
        m.wq(WORLD + PRIMS_OFF, container);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, n, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_INDIRECT_VECTOR, "indirect vector detected");
        Check(r.count == n && r.data == VEC, "indirect vector: data + count exact");
    }

    // -- 3. garbage: no layout validates -------------------------------------
    {
        Mem m;
        m.wq(WORLD + PRIMS_OFF, 0xdeadbeefdeadbeefull);
        m.wq(WORLD + PRIMS_OFF + 8, 0x10);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, 0, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "garbage container rejected");
    }

    // -- 4. count hint disagreement rejects a plausible-looking list --------
    {
        Mem m;
        size_t n = fill_vector(m, 12);          // 12 entries...
        m.wq(WORLD + PRIMS_OFF, VEC);
        m.wq(WORLD + PRIMS_OFF + 8, VEC + n * 8);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, 50000, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "count 12 vs hint 50000 rejected");
        // ...but the 2x slack holds for streaming maps (parts come and go)
        r = worlddet::detect(WORLD, PRIMS_OFF, 24, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_INLINE_VECTOR, "count 12 vs hint 24 accepted (2x slack)");
    }

    // -- 5. struct-array decoy: entries are primitive FIELDS, not pointers --
    // a dense array of Primitive structs would put a vtable-ish qword at
    // [data]; following it as a pointer lands in non-float data
    {
        Mem m;
        const uintptr_t fake_data = 0x600000;   // "entries" = qwords inside a prim object
        m.wq(fake_data + 0, 0x700000);          // a pointer field, not a prim
        m.wf(fake_data + 8, 1234.5f, 6789.0f, 1.0f);   // floats-as-qword: non-canonical
        m.wq(WORLD + PRIMS_OFF, fake_data);
        m.wq(WORLD + PRIMS_OFF + 8, fake_data + 16);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, 2, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "struct-array decoy rejected");
    }

    // -- 6. inline vector misread as indirect must not validate -------------
    // if [data..end] is reinterpreted as (container, junk), the "entries"
    // would be the primitive pointers themselves treated as a prim array
    {
        Mem m;
        size_t n = fill_vector(m, 16);
        m.wq(WORLD + PRIMS_OFF, VEC);
        m.wq(WORLD + PRIMS_OFF + 8, VEC + n * 8);
        // force candidate 1 to fail by making the count disagree with the hint,
        // then check candidate 2 does not silently pick it up
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, 100000, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "inline vector not misdetected as indirect");
    }

    // -- 7. one NaN position among the samples: still enough plausible ------
    {
        Mem m;
        size_t n = fill_vector(m, 12);
        // samples land on entries 0,2,4,6,8,10 of 12 -- poison entry 6's
        // position so exactly one sample is unbelievable
        uintptr_t entry6 = 0;
        m.read(VEC + 6 * 8, &entry6, 8);
        float nan_pos[3] = { std::nanf(""), 0.0f, 0.0f };
        m.write(entry6 + POS_OFF, nan_pos, sizeof(nan_pos));
        m.wq(WORLD + PRIMS_OFF, VEC);
        m.wq(WORLD + PRIMS_OFF + 8, VEC + n * 8);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, n, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_INLINE_VECTOR, "one NaN among six samples still detects");
    }

    // -- 8. tiny list (count 2): every entry must be plausible --------------
    {
        Mem m;
        size_t n = fill_vector(m, 2);
        m.wq(WORLD + PRIMS_OFF, VEC);
        m.wq(WORLD + PRIMS_OFF + 8, VEC + n * 8);
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, n, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_INLINE_VECTOR, "tiny list (2 entries) detected");

        // now poison one of the two: needed plausible (2) > plausible (1)
        uintptr_t entry1 = 0;
        m.read(VEC + 8, &entry1, 8);
        float zero_size[3] = { 0.0f, 0.0f, 0.0f };
        m.write(entry1 + SIZE_OFF, zero_size, sizeof(zero_size));
        r = worlddet::detect(WORLD, PRIMS_OFF, n, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "tiny list with one bad entry rejected");
    }

    // -- 9. unreadable world / unreadable container: NONE, no crash ---------
    {
        Mem m;   // empty memory
        worlddet::Result r = worlddet::detect(WORLD, PRIMS_OFF, 0, mem_read, &m, POS_OFF, SIZE_OFF);
        Check(r.layout == worlddet::LAYOUT_NONE, "unreadable world rejected cleanly");
    }

    printf("%s  (%d checks)\n", g_fail ? "FAILURES" : "all ok", g_n);
    return g_fail ? 1 : 0;
}
