// Pure parser regression from the operator receipt; never requires PID namespaces
// or privileged unshare in CI.
#define main diagnostic_main
#include "../../tools/sober_attach.cpp"
#undef main
#include <cassert>
int main() {
 assert(windowPidMatches("Uid:\t1000\nNSpid:\t6456\t3\n",3));
 assert(!windowPidMatches("NSpid:\t6456\t3\n",6456));
 assert(windowPidMatches("NSpid:\t50\t20\t3\n",3));
 assert(!windowPidMatches("NSpid:\t50\t20\t3\n",20));
 assert(windowPidMatches("NSpid:\t6456\n",6456));
 assert(!windowPidMatches("Name:\tMain\n",3));
 assert(!windowPidMatches("NSpid:\n",3));
 std::cout<<"NAMESPACE_PID_PASS: host, nested, absent, and empty fields\n";
}
