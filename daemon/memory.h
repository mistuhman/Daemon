// -----------------------------------------------------------------------------
//  memory.h -- Daemon's memory + instance API, Linux-only
//  The read/write primitives go through platform.h; the instance/child-walk
//  logic never knows the OS.
// -----------------------------------------------------------------------------
#pragma once

#include "platform/platform.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <atomic>

class MemoryClass {
public:
    void* Handle = nullptr;   // platform handle (opaque pid token on Linux)
    template <typename T> T Read(uint64_t address) {
        T out{};
        if (Handle) platform::ReadMem(Handle, address, &out, sizeof(T));
        return out;
    }
    template <typename T> void Write(uint64_t address, T value) {
        if (Handle) platform::WriteMem(Handle, address, &value, sizeof(T));
    }
    bool ReadRaw(uint64_t address, void* buffer, size_t size) {
        return Handle && platform::ReadMem(Handle, address, buffer, size);
    }
    bool WriteRaw(uint64_t address, const void* data, size_t size) {
        return Handle && platform::WriteMem(Handle, address, data, size);
    }
};

extern MemoryClass g_memory;

namespace mem {
    // On Linux, these are accessed through platform layer directly
    // Kept for compatibility with existing code
    extern std::atomic<void*> roblox_h;      // platform handle to the game
    extern std::atomic<uint32_t> process_id;
    bool grabroblox_h();
}

bool is_valid_address(uintptr_t address);

std::string fetchstring(uint64_t address);

template <typename T> T read(uint64_t address) {
    if (!is_valid_address(address)) return T{};
    return g_memory.Read<T>(address);
}

template <typename T> bool write(uint64_t address, T value) {
    if (!is_valid_address(address)) return false;
    g_memory.Write(address, value);
    return true;
}

// drops cached class-name lookups (call when re-attaching to a new process)
void clear_instance_caches();

bool read_raw(uint64_t address, void* buffer, size_t size);
bool write_raw(uint64_t address, const void* data, size_t size);

struct instance {
    uintptr_t address = 0;
    bool is_valid() const { return address != 0; }
    std::string get_name() const;
    std::string get_class_name() const;
    std::vector<instance> get_children() const;
    instance read_child(const std::string& child_name) const;
    instance read_service(const std::string& service_name) const;
    instance model_instance() const;
    instance local_player() const;
};