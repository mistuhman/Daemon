// tools/world_ray_test.cpp — compiles the world-ray + feature geometry harness
// g++ -std=c++17 -O1 -w -I"../daemon" -I"tools/win32_stub" tools/world_ray_test.cpp -o build/world_ray_test

#include "daemon/features/raycast/world_ray.h"
#include "daemon/features/esp/range.h"
#include "daemon/features/fov/fov_encode.h"
#include "daemon/features/misc/body_rules.h"
#include "daemon/features/flight/stealth.h"
#include "daemon/features/players/humanoid_state.h"
#include "daemon/features/fpscap/fps_field.h"
#include "daemon/features/world/sky.h"
#include <iostream>

int main() {
    std::cout << "world_ray test harness compiles" << std::endl;
    return 0;
}
