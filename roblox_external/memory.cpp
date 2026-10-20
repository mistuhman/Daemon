#include "memory.h"
#include "globals.h"
#include "offsets.h"
#ifdef __linux__
#include "platform/sober_scanner.h"
#endif
#include <unordered_map>
#include <mutex>

static std::mutex g_classname_mutex;
static std::unordered_map<uintptr_t, std::string> g_classname_cache;

MemoryClass g_memory;

namespace mem {
    std::atomic<void*> roblox_h{ nullptr };
    std::atomic<uint32_t> process_id{ 0 };
}

bool is_valid_address(uintptr_t address) {
    return address != 0 && address >= 0x10000 && address < 0x7FFFFFFFFFFF;
}

static std::string read_string_raw(uint64_t address) {
    std::string s;
    static constexpr int sizes[] = { 128, 64, 32, 16 };
    char buf[128];
    for (int want : sizes) {
        if (!g_memory.ReadRaw(address, buf, (size_t)want)) continue;
        for (int i = 0; i < want; ++i) {
            if (buf[i] == 0) return s;
            s.push_back(buf[i]);
        }
        return s;
    }
    return s;
}

std::string fetchstring(uint64_t address) {
#ifdef __linux__
    // Sober pooled strings: try pool first, fall back to Windows std::string layout.
    if (g_memory.Handle) {
        std::string pooled = sober::TryPooledString(g_memory.Handle, address);
        if (!pooled.empty()) return pooled;
    }
#endif
    int length = read<int>(address + 0x18);
    if (length >= 16u) {
        uintptr_t padding = read<uintptr_t>(address);
        return read_string_raw(padding);
    }
    return read_string_raw(address);
}

std::string instance::get_name() const {
    uintptr_t container = read<uintptr_t>(address + Offsets::Instance::NameContainer);
    if (!is_valid_address(container)) return {};
    return fetchstring(container + Offsets::Instance::Name);
}

std::string instance::get_class_name() const {
    uintptr_t descriptor = read<uintptr_t>(address + Offsets::Instance::ClassDescriptor);
    if (!is_valid_address(descriptor)) return {};

    {
        std::lock_guard<std::mutex> lock(g_classname_mutex);
        auto it = g_classname_cache.find(descriptor);
        if (it != g_classname_cache.end()) return it->second;
    }

    std::string name = fetchstring(read<uintptr_t>(descriptor + Offsets::Instance::ClassName));
    if (!name.empty()) {
        std::lock_guard<std::mutex> lock(g_classname_mutex);
        if (g_classname_cache.size() > 4096) g_classname_cache.clear();
        g_classname_cache[descriptor] = name;
    }
    return name;
}

void clear_instance_caches() {
    std::lock_guard<std::mutex> lock(g_classname_mutex);
    g_classname_cache.clear();
}

instance instance::read_child(const std::string& child_name) const {
    for (auto child : get_children()) {
        if (child.get_name() == child_name) return child;
    }
    return instance{};
}

instance instance::model_instance() const {
    return read<instance>(address + Offsets::Player::ModelInstance);
}

instance instance::local_player() const {
    return read<instance>(address + Offsets::Player::LocalPlayer);
}

std::vector<instance> instance::get_children() const {
    std::vector<instance> children;
    if (!is_valid_address(address)) return children;

    uint64_t container = read<uint64_t>(address + Offsets::Instance::ChildrenStart);
    if (!is_valid_address(container)) return children;

    uint64_t data_ptr = read<uint64_t>(container);
    uint64_t end_ptr = read<uint64_t>(container + Offsets::Instance::ChildrenEnd);
    if (!is_valid_address(data_ptr) || !is_valid_address(end_ptr)) return children;
    if (end_ptr <= data_ptr) return children;

    uint64_t max_end = data_ptr + 4096 * 16u;
    if (end_ptr > max_end) end_ptr = max_end;

    size_t count = (size_t)((end_ptr - data_ptr) / 16u);
    if (count == 0) return children;

    static thread_local std::vector<uint64_t> raw;
    raw.resize(count * 2);
    if (!g_memory.ReadRaw(data_ptr, raw.data(), count * 16u)) {
        for (uint64_t at = data_ptr; at < end_ptr; at += 16u) {
            instance child = read<instance>(at);
            if (child.is_valid()) children.push_back(child);
        }
        return children;
    }

    children.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        uint64_t ptr = raw[i * 2];
        if (is_valid_address((uintptr_t)ptr)) children.push_back(instance{ (uintptr_t)ptr });
    }
    return children;
}

instance instance::read_service(const std::string& service_name) const {
    instance returned{};
    for (auto child : get_children()) {
        if (child.get_class_name() == service_name) return child;
    }
    return returned;
}

bool read_raw(uint64_t address, void* buffer, size_t size) {
    if (!is_valid_address(address)) return false;
    return g_memory.ReadRaw(address, buffer, size);
}

bool write_raw(uint64_t address, const void* data, size_t size) {
    if (!is_valid_address(address)) return false;
    return g_memory.WriteRaw(address, data, size);
}

bool mem::grabroblox_h() {
    uint32_t target_pid = process_id.load();
    void* h = nullptr;
    if (platform::OpenGameHandle(target_pid, &h)) {
        LogLine("grabroblox_h: opened handle %p for pid %u", h, target_pid);
        void* old = roblox_h.exchange(h);
        platform::CloseHandle(old);
        g_memory.Handle = roblox_h.load();
        platform::SetGamePid(target_pid);
        return true;
    }
    LogLine("grabroblox_h: open FAILED for pid %u (run as administrator?)", target_pid);
    return false;
}
