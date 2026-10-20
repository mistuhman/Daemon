#pragma once
// Linux ELF inspection shared by diagnostics and attachment. No game offsets.
#ifdef __linux__
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <string>
#include <vector>
#include <utility>
#include <sys/uio.h>
#include <unistd.h>
namespace linux_elf {
struct Image {
    uintptr_t bias = 0;
    bool valid = false;
    std::string soname;
    std::string build_id;
    std::vector<std::pair<uintptr_t, uintptr_t>> executable_ranges;
};
inline bool Read(unsigned pid, uintptr_t address, void* out, size_t size) {
    if (size > UINTPTR_MAX - address) return false;
    iovec local{out, size}, remote{reinterpret_cast<void*>(address), size};
    return process_vm_readv(pid, &local, 1, &remote, 1, 0) == static_cast<ssize_t>(size);
}
inline Image Inspect(unsigned pid, uintptr_t headerAddress) {
    Image image;
    Elf64_Ehdr header{};
    if (!Read(pid, headerAddress, &header, sizeof(header)) ||
        std::memcmp(header.e_ident, ELFMAG, SELFMAG) ||
        header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_ident[EI_DATA] != ELFDATA2LSB ||
        header.e_ident[EI_VERSION] != EV_CURRENT || header.e_version != EV_CURRENT ||
        (header.e_type != ET_DYN && header.e_type != ET_EXEC) ||
        header.e_phentsize != sizeof(Elf64_Phdr) || !header.e_phnum || header.e_phnum > 256 ||
        header.e_phoff > 1048576 || header.e_phoff > UINTPTR_MAX - headerAddress) return image;
    std::vector<Elf64_Phdr> ph(header.e_phnum);
    if (!Read(pid, headerAddress + header.e_phoff, ph.data(), ph.size() * sizeof(Elf64_Phdr))) return image;
    for (const auto& p : ph) {
        if (p.p_type != PT_LOAD) continue;
        if (p.p_filesz > p.p_memsz || p.p_vaddr > UINTPTR_MAX - p.p_memsz) return image;
        if (p.p_offset == 0 && p.p_filesz >= sizeof(header) && p.p_vaddr <= headerAddress) {
            if (image.valid && image.bias != headerAddress - p.p_vaddr) return Image{};
            image.bias = headerAddress - p.p_vaddr;
            image.valid = true;
        }
    }
    if (!image.valid) return image;
    for (const auto& p : ph) {
        if (p.p_type != PT_LOAD || !(p.p_flags & PF_X)) continue;
        if (p.p_vaddr > UINTPTR_MAX - image.bias || p.p_memsz > UINTPTR_MAX - (image.bias + p.p_vaddr)) return Image{};
        image.executable_ranges.emplace_back(image.bias + p.p_vaddr, image.bias + p.p_vaddr + p.p_memsz);
    }
    auto inside = [&](uintptr_t address, size_t size) {
        for (const auto& p : ph) {
            if (p.p_type != PT_LOAD || p.p_vaddr > UINTPTR_MAX - image.bias) continue;
            auto begin = image.bias + p.p_vaddr;
            if (address >= begin && address - begin <= p.p_memsz &&
                size <= p.p_memsz - (address - begin) && size <= UINTPTR_MAX - address) return true;
        }
        return false;
    };
    auto read = [&](uintptr_t address, void* out, size_t size) {
        return inside(address, size) && Read(pid, address, out, size);
    };
    for (const auto& p : ph) {
        if (p.p_type != PT_DYNAMIC || p.p_vaddr > UINTPTR_MAX - image.bias) continue;
        auto dynamicAddress = image.bias + p.p_vaddr;
        uintptr_t strings = 0;
        size_t length = 0, nameOffset = 0;
        bool hasName = false, terminated = false;
        for (size_t i = 0; i < std::min<size_t>(p.p_memsz / sizeof(Elf64_Dyn), 4096); ++i) {
            Elf64_Dyn dyn{};
            auto offset = i * sizeof(dyn);
            if (offset > UINTPTR_MAX - dynamicAddress || !read(dynamicAddress + offset, &dyn, sizeof(dyn))) break;
            if (dyn.d_tag == DT_NULL) { terminated = true; break; }
            if (dyn.d_tag == DT_STRTAB) strings = dyn.d_un.d_ptr;
            if (dyn.d_tag == DT_STRSZ) length = dyn.d_un.d_val;
            if (dyn.d_tag == DT_SONAME) { nameOffset = dyn.d_un.d_val; hasName = true; }
        }
        if (!terminated || !hasName || !strings || nameOffset >= length || length > 64 * 1024 * 1024) continue;
        std::vector<uintptr_t> choices;
        if (inside(strings, length)) choices.push_back(strings);
        if (strings <= UINTPTR_MAX - image.bias && inside(image.bias + strings, length) &&
            (choices.empty() || choices[0] != image.bias + strings)) choices.push_back(image.bias + strings);
        if (choices.size() != 1) continue; // Never choose an ambiguous pointer representation.
        std::string name;
        for (size_t i = 0; i < std::min<size_t>(length - nameOffset, 512); ++i) {
            char ch;
            if (!read(choices[0] + nameOffset + i, &ch, 1)) break;
            if (!ch) { image.soname = name; break; }
            if (static_cast<unsigned char>(ch) < 32 || static_cast<unsigned char>(ch) == 127) break;
            name += ch;
        }
    }
    for (const auto& p : ph) {
        if (p.p_type != PT_NOTE || !p.p_filesz || p.p_filesz > 65536 || p.p_vaddr > UINTPTR_MAX - image.bias) continue;
        std::vector<unsigned char> notes(p.p_filesz);
        if (!read(image.bias + p.p_vaddr, notes.data(), notes.size())) continue;
        size_t at = 0;
        while (at + sizeof(Elf64_Nhdr) <= notes.size()) {
            Elf64_Nhdr note;
            std::memcpy(&note, notes.data() + at, sizeof(note));
            at += sizeof(note);
            size_t names = (size_t(note.n_namesz) + 3) & ~size_t(3);
            size_t desc = (size_t(note.n_descsz) + 3) & ~size_t(3);
            if (names > notes.size() - at || desc > notes.size() - at - names) break;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 &&
                !std::memcmp(notes.data() + at, "GNU", 4)) {
                static const char hex[] = "0123456789abcdef";
                for (size_t i = 0; i < note.n_descsz; ++i) {
                    auto c = notes[at + names + i];
                    image.build_id += hex[c >> 4]; image.build_id += hex[c & 15];
                }
            }
            at += names + desc;
        }
    }
    return image;
}
} // namespace linux_elf
#endif
