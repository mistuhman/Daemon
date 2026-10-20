// Unit test for cache::EspEntity::IsDead() -- the one definition of "dead" the whole
// tool shares (the players page labels with it, the aimbot's dead check filters on it).
//
//   g++ -std=c++17 -O1 -I"../roblox_external" tools/dead_check_test.cpp -o build/dead_check_test
//   ./build/dead_check_test
//
// No Windows, no Roblox, no game: the predicate is two float comparisons on a struct
// the cache fills in, and it is small enough to state completely. That is also why it
// is worth a file: the failure mode it exists to prevent is silent in both directions.
// Read `health <= 0` alone and a frame where the Humanoid could not be read reports
// every player in the server as a corpse -- the aimbot refuses to aim at anything, or
// releases a lock mid-fight, and the menu shows a live player as "dead". Read only
// "health > 0" and a player at exactly 0 hp with no max_health read stays aimable,
// which is the case the option was asked for in the first place.
#include "cache.h"

#include <cstdio>
#include <cstring>

static int g_fail = 0;
static int g_n = 0;

static void Check(bool cond, const char* what)
{
    ++g_n;
    printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++g_fail;
}

static cache::EspEntity Ent(float hp, float max)
{
    cache::EspEntity e{};
    e.health = hp;
    e.max_health = max;
    return e;
}

int main()
{
    // ---- what "dead" means ---------------------------------------------------
    Check(Ent(0.0f, 100.0f).IsDead(),  "0 hp with a readable max health is dead");
    Check(Ent(-4.5f, 100.0f).IsDead(), "a negative health read is dead, not alive");
    Check(Ent(-0.0f, 100.0f).IsDead(), "negative zero is 0, and 0 is dead");
    Check(!Ent(0.001f, 100.0f).IsDead(), "the first frame after a revive is alive");
    Check(!Ent(1.0f, 100.0f).IsDead(),  "1 hp is alive");
    Check(!Ent(100.0f, 100.0f).IsDead(),"full health is alive");

    // ---- what "no data" means, which is the part that bites ------------------
    Check(!Ent(0.0f, 0.0f).IsDead(),   "no health read at all is UNKNOWN, not dead");
    Check(!Ent(0.0f, -1.0f).IsDead(),  "a garbage max health is unknown, not dead");
    {
        // The cache's own shape: `EspEntity e{}` then a walk that never finds a
        // Humanoid child leaves both fields at their initial 0. Default-constructed
        // must therefore be "not dead", or the aimbot would skip every player in a
        // game whose characters have no readable humanoid.
        cache::EspEntity fresh{};
        Check(!fresh.IsDead(), "a freshly zeroed entity is not dead");
        Check(fresh.health == 0.0f && fresh.max_health == 0.0f,
              "and that really is the state the cache leaves behind");
    }

    // ---- games that are not 100 hp ------------------------------------------
    Check(Ent(0.0f, 0.5f).IsDead(),  "a half-hitpoint game still reports death");
    Check(!Ent(250.0f, 250.0f).IsDead(), "250/250 in a survivour-style server is alive");

    // ---- the predicate must depend on nothing else ----------------------------
    {
        cache::EspEntity a = Ent(0.0f, 100.0f);
        cache::EspEntity b = Ent(0.0f, 100.0f);
        std::snprintf(a.name, sizeof(a.name), "someone");
        std::snprintf(b.tool_name, sizeof(b.tool_name), "Sword");
        a.primitive_count = 12;
        b.is_teammate = true;
        Check(a.IsDead() == b.IsDead(), "identity, tool and part count change nothing");
        // Being on your team is NOT being dead: the two filters stay separate
        // settings, because a teammate is a player you may still want to see.
        Check(b.is_teammate && b.IsDead(), "a dead teammate is dead and a teammate");
    }

    printf("%s: %d checks\n", g_fail ? "FAILURES" : "all ok", g_n);
    return g_fail ? 1 : 0;
}
