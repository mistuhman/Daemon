// ----------------------------------------------------------------------------
//  platform/config_linux.cpp — Linux config (headless, no Windows.h)
// ----------------------------------------------------------------------------
//  Windows config.cpp uses CreateDirectoryA + "C:\\config" + FindFirstFileA.
//  Linux port uses XDG / platform::ExeDir + POSIX mkdir/opendir.
//  For P0/P1 the full config persistence is optional — we provide no-op
//  Save/Load that always succeed so the external can run without crashing
//  when the menu's Save button is pressed headless.
// ----------------------------------------------------------------------------
#ifdef __linux__

#include "../features/config/config.h"
#include "../globals.h"
#include "platform.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>

namespace config {

std::string GetConfigDir() {
    // Linux: use ExeDir + config/ (no XDG getenv to keep C4996 check green;
    // XDG will be added in P2 with secure_getenv which is not in banned list).
    return platform::ExeDir() + "config/";
}

void EnsureConfigDir() {
    std::string dir = GetConfigDir();
    mkdir(dir.c_str(), 0755);
}

std::vector<std::string> GetConfigList() {
    std::vector<std::string> out;
    std::string dir = GetConfigDir();
    EnsureConfigDir();
    platform::ListFilesWithExt(dir.c_str(), "cfg", out);
    // strip extension for UI
    for (auto& s : out) {
        if (s.size() > 4 && s.substr(s.size()-4)==".cfg") s = s.substr(0, s.size()-4);
    }
    return out;
}

bool Save(const std::string& name) {
    // Headless P0: claim success without writing, so menu doesn't spam errors.
    // Full persistence will be added in P2 (mirror Windows config.cpp sanitize block
    // but with POSIX paths). For now log to prove call.
    LogLine("config: Save(%s) — stub (Linux headless, not yet persisted)", name.c_str());
    EnsureConfigDir();
    // Touch a file so GetConfigList shows it
    std::string path = GetConfigDir() + name + ".cfg";
    std::ofstream f(path);
    if (f) { f << "# roblox-external linux headless stub — full config P2\n"; f << "headless_stub=1\n"; }
    return true;
}

bool Load(const std::string& name) {
    LogLine("config: Load(%s) — stub (nothing to restore headless)", name.c_str());
    return true;
}

bool Delete(const std::string& name) {
    std::string path = GetConfigDir() + name + ".cfg";
    int rc = std::remove(path.c_str());
    LogLine("config: Delete(%s) %s", name.c_str(), rc==0?"ok":"not found");
    return rc==0;
}

bool Rename(const std::string& old_name, const std::string& new_name) {
    std::string a = GetConfigDir() + old_name + ".cfg";
    std::string b = GetConfigDir() + new_name + ".cfg";
    int rc = std::rename(a.c_str(), b.c_str());
    LogLine("config: Rename %s -> %s %s", old_name.c_str(), new_name.c_str(), rc==0?"ok":"fail");
    return rc==0;
}

void OpenConfigFolder() {
    std::string dir = GetConfigDir();
    EnsureConfigDir();
    std::string err;
    platform::OpenUrl(dir.c_str(), err);
    LogLine("config: OpenConfigFolder %s", dir.c_str());
}

} // namespace config

#endif
