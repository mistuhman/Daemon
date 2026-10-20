// -----------------------------------------------------------------------------
//  offsets_live.cpp — Linux offset version reporting
// -----------------------------------------------------------------------------
#include "offsets_live.h"
#include "offsets.h"
#include "offsets_dynamic.h"

namespace offsets_live {

const char* OffsetsLoadedVersion() {
    // On Linux, the "loaded" version is the one offsets_dynamic discovered
    // The version string is in offsets.h's ClientVersion (fallback) or dynamic
    static std::string s_version;
    if (s_version.empty()) {
        const char* cv = Offsets::ClientVersion.c_str();
        s_version = cv && cv[0] ? cv : "linux-dynamic";
    }
    return s_version.c_str();
}

const char* OffsetsClientVersion() {
    // The actual Sober/ELF version - not directly readable without a running process
    // For now, return the same as loaded version
    static std::string s_version = "sober-unknown";
    return s_version.c_str();
}

} // namespace offsets_live