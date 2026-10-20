// tools/dead_check_test.cpp — compiles the dead-check logic against the app headers
// g++ -std=c++17 -O1 -I"../daemon" tools/dead_check_test.cpp -o build/dead_check_test

#include "daemon/features/players/players.h"
#include "daemon/features/players/friends.h"

int main() {
    using namespace features::players;
    bool dead = false;
    int state = 0;
    bool is_friend = false;
    CheckDead(dead, state, is_friend, 0);
    return 0;
}
