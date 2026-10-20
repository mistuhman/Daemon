// tools/tp_math_test.cpp — compiles the click-teleport math
// g++ -std=c++17 -O1 -I"../daemon" tools/tp_math_test.cpp -o build/tp_math_test

#include "daemon/features/click_teleport/tp_math.h"
#include <iostream>

int main() {
    using namespace features::click_teleport;
    std::cout << "tp_math compiles" << std::endl;
    return 0;
}
