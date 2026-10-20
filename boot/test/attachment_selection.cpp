#include "platform/sober_scanner.h"
#include <iostream>
int main() {
    const auto pid = sober::FindSoberPid();
    std::cout << "pid=" << pid << " base=" << sober::LibRobloxBase(pid) << '\n';
}
