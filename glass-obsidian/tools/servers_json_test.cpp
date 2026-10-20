// tools/servers_json_test.cpp — compiles the servers JSON logic
// g++ -std=c++17 -O1 -w -I"../daemon" tools/servers_json_test.cpp -o build/servers_json_test

#include "daemon/features/servers/servers_json.h"
#include <iostream>

int main() {
    using namespace features::servers::json;
    std::cout << "servers_json compiles" << std::endl;
    return 0;
}
