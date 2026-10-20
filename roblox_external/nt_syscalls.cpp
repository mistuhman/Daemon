// ─────────────────────────────────────────────────────────────────────────────
//  nt_syscalls.cpp — NTDLL unhooking + indirect syscall dispatch
// ─────────────────────────────────────────────────────────────────────────────

#include "nt_syscalls.h"
#include "globals.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <atomic>

// ── ASM dispatch stubs (syscall_stub.asm) ────────────────────────────────────
extern "C" {
    NTSTATUS IndirectSyscall(void* syscall_addr, DWORD ssn, ...);
    NTSTATUS IndirectSyscall4(void* syscall_addr, DWORD ssn, ...);
    NTSTATUS IndirectSyscall6(void* syscall_addr, DWORD ssn, ...);
}

// ── Syscall table ────────────────────────────────────────────────────────────
namespace nt {
    SyscallEntry sc_NtOpenProcess{};
    SyscallEntry sc_NtReadVirtualMemory{};
    SyscallEntry sc_NtWriteVirtualMemory{};
    SyscallEntry sc_NtAllocateVirtualMemory{};
    SyscallEntry sc_NtFreeVirtualMemory{};
    SyscallEntry sc_NtClose{};
    SyscallEntry sc_NtQueryInformationProcess{};
    SyscallEntry sc_NtQuerySystemInformation{};
    SyscallEntry sc_NtQueryVirtualMemory{};
    SyscallEntry sc_NtProtectVirtualMemory{};
}

// Rate-limited error logging (avoids spamming the log on repeated failures)
static std::atomic<int> g_read_err_count{0};
static std::atomic<NTSTATUS> g_last_read_status{0};

// ─────────────────────────────────────────────────────────────────────────────
//  NTDLL Unhooking
//
//  Maps a fresh copy of ntdll.dll from C:\Windows\System32\ntdll.dll and
//  overwrites the .text section of the loaded copy.  This removes any
//  hooks that Hyperion/Byfron has placed on Nt* functions.
// ─────────────────────────────────────────────────────────────────────────────

static bool UnhookNtdll()
{
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) {
        LogLine("unhook: GetModuleHandle(ntdll) failed");
        return false;
    }

    HANDLE hFile = CreateFileA(
        "C:\\Windows\\System32\\ntdll.dll",
        GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogLine("unhook: CreateFile failed (err %lu)", GetLastError());
        return false;
    }

    HANDLE hMapping = CreateFileMappingA(
        hFile, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
    if (!hMapping) {
        LogLine("unhook: CreateFileMapping failed (err %lu)", GetLastError());
        CloseHandle(hFile);
        return false;
    }

    void* pFreshNtdll = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!pFreshNtdll) {
        LogLine("unhook: MapViewOfFile failed (err %lu)", GetLastError());
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return false;
    }

    // Parse PE headers of the LOADED ntdll
    auto dosHeader = (IMAGE_DOS_HEADER*)hNtdll;
    auto ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)hNtdll + dosHeader->e_lfanew);
    auto sections = IMAGE_FIRST_SECTION(ntHeaders);

    // Parse PE headers of the FRESH ntdll
    auto freshDos = (IMAGE_DOS_HEADER*)pFreshNtdll;
    auto freshNt  = (IMAGE_NT_HEADERS*)((BYTE*)pFreshNtdll + freshDos->e_lfanew);
    auto freshSections = IMAGE_FIRST_SECTION(freshNt);

    bool success = false;
    for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; ++i) {
        if (memcmp(sections[i].Name, ".text", 5) != 0)
            continue;

        // Find the .text section in the FRESH image by name (don't assume
        // same index — section order can differ between loaded and mapped)
        void* pFreshText = nullptr;
        SIZE_T freshSize = 0;
        for (WORD j = 0; j < freshNt->FileHeader.NumberOfSections; ++j) {
            if (memcmp(freshSections[j].Name, ".text", 5) == 0) {
                pFreshText = (BYTE*)pFreshNtdll + freshSections[j].VirtualAddress;
                freshSize  = freshSections[j].Misc.VirtualSize;
                break;
            }
        }
        if (!pFreshText) {
            LogLine("unhook: .text not found in fresh ntdll");
            break;
        }

        void* pTextSection = (BYTE*)hNtdll + sections[i].VirtualAddress;
        SIZE_T textSize    = sections[i].Misc.VirtualSize;

        // Use the smaller of the two sizes to avoid overread/overwrite
        SIZE_T copySize = (textSize < freshSize) ? textSize : freshSize;

        LogLine("unhook: .text at loaded+0x%X (%zu bytes), fresh+0x%X (%zu bytes), copying %zu",
                sections[i].VirtualAddress, textSize,
                (unsigned)((BYTE*)pFreshText - (BYTE*)pFreshNtdll), freshSize,
                copySize);

        DWORD oldProtect = 0;
        if (!VirtualProtect(pTextSection, copySize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            LogLine("unhook: VirtualProtect failed (err %lu)", GetLastError());
            break;
        }

        memcpy(pTextSection, pFreshText, copySize);

        DWORD dummy = 0;
        VirtualProtect(pTextSection, copySize, oldProtect, &dummy);

        success = true;
        break;
    }

    UnmapViewOfFile(pFreshNtdll);
    CloseHandle(hMapping);
    CloseHandle(hFile);

    return success;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SSN Resolution
// ─────────────────────────────────────────────────────────────────────────────

static bool ResolveSSN(const char* name, SyscallEntry& out)
{
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return false;

    auto dosHeader = (IMAGE_DOS_HEADER*)hNtdll;
    auto ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)hNtdll + dosHeader->e_lfanew);
    auto& exportDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!exportDir.Size) return false;

    auto exports = (IMAGE_EXPORT_DIRECTORY*)((BYTE*)hNtdll + exportDir.VirtualAddress);
    auto names   = (DWORD*)((BYTE*)hNtdll + exports->AddressOfNames);
    auto ordinals = (WORD*)((BYTE*)hNtdll + exports->AddressOfNameOrdinals);
    auto functions = (DWORD*)((BYTE*)hNtdll + exports->AddressOfFunctions);

    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const char* funcName = (const char*)((BYTE*)hNtdll + names[i]);
        if (strcmp(funcName, name) != 0)
            continue;

        BYTE* funcAddr = (BYTE*)hNtdll + functions[ordinals[i]];

        DWORD ssn = 0;
        void* syscall_addr = nullptr;

        // Pattern 1: Clean prologue — 4C 8B D1 B8 XX XX 00 00
        if (funcAddr[0] == 0x4C && funcAddr[1] == 0x8B && funcAddr[2] == 0xD1 &&
            funcAddr[3] == 0xB8) {
            ssn = *(DWORD*)(funcAddr + 4);
        }
        // Pattern 2: Hooked (JMP) — E9 XX XX XX XX, SSN at offset +12 or +8
        else if (funcAddr[0] == 0xE9) {
            if (funcAddr[12] == 0xB8)
                ssn = *(DWORD*)(funcAddr + 13);
            else if (funcAddr[8] == 0xB8)
                ssn = *(DWORD*)(funcAddr + 9);
            else {
                LogLine("ssn: %s has JMP hook but no B8 at +8 or +12 (bytes: %02X %02X %02X)",
                        name, funcAddr[8], funcAddr[9], funcAddr[12]);
                return false;
            }
        }
        else {
            LogLine("ssn: %s has unknown prologue: %02X %02X %02X %02X",
                    name, funcAddr[0], funcAddr[1], funcAddr[2], funcAddr[3]);
            return false;
        }

        // Find `syscall; ret` (0F 05 C3) within the function
        for (int j = 0; j < 64; ++j) {
            if (funcAddr[j] == 0x0F && funcAddr[j+1] == 0x05 && funcAddr[j+2] == 0xC3) {
                syscall_addr = funcAddr + j;
                break;
            }
        }

        if (!syscall_addr) {
            LogLine("ssn: %s has no syscall;ret in first 64 bytes", name);
            return false;
        }

        out.ssn = ssn;
        out.syscall_addr = syscall_addr;
        out.valid = true;
        LogLine("ssn: %-30s = 0x%04X  syscall@%p", name, ssn, syscall_addr);
        return true;
    }

    LogLine("ssn: %s not found in ntdll exports", name);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialization
// ─────────────────────────────────────────────────────────────────────────────

bool nt::Init()
{
    if (!UnhookNtdll()) {
        LogLine("nt: FATAL — ntdll unhook failed");
        return false;
    }
    LogLine("nt: ntdll.dll unhooked successfully");

    struct { const char* name; SyscallEntry* entry; } table[] = {
        { "NtOpenProcess",              &sc_NtOpenProcess },
        { "NtReadVirtualMemory",        &sc_NtReadVirtualMemory },
        { "NtWriteVirtualMemory",       &sc_NtWriteVirtualMemory },
        { "NtAllocateVirtualMemory",    &sc_NtAllocateVirtualMemory },
        { "NtFreeVirtualMemory",        &sc_NtFreeVirtualMemory },
        { "NtClose",                    &sc_NtClose },
        { "NtQueryInformationProcess",  &sc_NtQueryInformationProcess },
        { "NtQuerySystemInformation",   &sc_NtQuerySystemInformation },
        { "NtQueryVirtualMemory",       &sc_NtQueryVirtualMemory },
        { "NtProtectVirtualMemory",     &sc_NtProtectVirtualMemory },
    };

    int resolved = 0;
    for (auto& item : table) {
        if (ResolveSSN(item.name, *item.entry))
            ++resolved;
    }

    int total = (int)(sizeof(table) / sizeof(table[0]));
    LogLine("nt: resolved %d/%d syscalls", resolved, total);
    return resolved == total;
}

// ─────────────────────────────────────────────────────────────────────────────
//  NT API Wrappers
// ─────────────────────────────────────────────────────────────────────────────

bool nt::OpenProcess(DWORD pid, DWORD access, HANDLE* out_handle)
{
    if (!sc_NtOpenProcess.valid) {
        LogLine("nt::OpenProcess: syscall not initialised");
        return false;
    }

    OBJECT_ATTRIBUTES oa{};
    oa.Length = sizeof(oa);
    CLIENT_ID cid{};
    cid.UniqueProcess = (HANDLE)(uintptr_t)pid;
    cid.UniqueThread = nullptr;

    NTSTATUS status = IndirectSyscall4(
        sc_NtOpenProcess.syscall_addr,
        sc_NtOpenProcess.ssn,
        out_handle,
        (ACCESS_MASK)access,
        &oa,
        &cid);

    if (!NT_SUCCESS(status))
        LogLine("nt::OpenProcess(pid=%u, access=0x%X) → 0x%08X", pid, access, (unsigned)status);

    return NT_SUCCESS(status);
}

bool nt::ReadProcessMemory(HANDLE process, uint64_t address,
                           void* buffer, size_t size, size_t* bytes_read)
{
    if (!sc_NtReadVirtualMemory.valid) return false;

    SIZE_T bytesReadLocal = 0;
    NTSTATUS status = IndirectSyscall6(
        sc_NtReadVirtualMemory.syscall_addr,
        sc_NtReadVirtualMemory.ssn,
        process,
        (PVOID)address,
        buffer,
        size,
        &bytesReadLocal,
        (PVOID)nullptr);

    if (bytes_read) *bytes_read = bytesReadLocal;

    if (!NT_SUCCESS(status)) {
        g_last_read_status.store(status, std::memory_order_relaxed);
        int n = g_read_err_count.fetch_add(1, std::memory_order_relaxed);
        // Log the first 5 errors, then every 100th
        if (n < 5 || (n % 100) == 0)
            LogLine("nt::Read(addr=0x%llX, size=%zu) → 0x%08X (err #%d)",
                    (unsigned long long)address, size, (unsigned)status, n);
    }

    return NT_SUCCESS(status);
}

bool nt::WriteProcessMemory(HANDLE process, uint64_t address,
                            const void* data, size_t size, size_t* bytes_written)
{
    if (!sc_NtWriteVirtualMemory.valid) return false;

    SIZE_T bytesWrittenLocal = 0;
    NTSTATUS status = IndirectSyscall6(
        sc_NtWriteVirtualMemory.syscall_addr,
        sc_NtWriteVirtualMemory.ssn,
        process,
        (PVOID)address,
        (PVOID)data,
        size,
        &bytesWrittenLocal,
        (PVOID)nullptr);

    if (bytes_written) *bytes_written = bytesWrittenLocal;
    return NT_SUCCESS(status);
}

bool nt::AllocateVirtualMemory(HANDLE process, void** base_address,
                               size_t size, DWORD allocation_type,
                               DWORD protect)
{
    if (!sc_NtAllocateVirtualMemory.valid) return false;

    PVOID base = nullptr;
    SIZE_T regionSize = size;

    NTSTATUS status = IndirectSyscall6(
        sc_NtAllocateVirtualMemory.syscall_addr,
        sc_NtAllocateVirtualMemory.ssn,
        process,
        &base,
        (ULONG_PTR)0,
        &regionSize,
        (ULONG)allocation_type,
        (ULONG)protect);

    if (NT_SUCCESS(status) && base_address)
        *base_address = base;

    return NT_SUCCESS(status);
}

bool nt::FreeVirtualMemory(HANDLE process, void** base_address,
                           size_t size, DWORD free_type)
{
    if (!sc_NtFreeVirtualMemory.valid) return false;

    SIZE_T regionSize = size;
    NTSTATUS status = IndirectSyscall4(
        sc_NtFreeVirtualMemory.syscall_addr,
        sc_NtFreeVirtualMemory.ssn,
        process,
        base_address,
        &regionSize,
        free_type);

    return NT_SUCCESS(status);
}

bool nt::CloseHandle(HANDLE handle)
{
    if (!sc_NtClose.valid) return false;

    NTSTATUS status = IndirectSyscall(
        sc_NtClose.syscall_addr,
        sc_NtClose.ssn,
        handle);

    return NT_SUCCESS(status);
}

bool nt::GetProcessExitStatus(HANDLE process, DWORD* exit_code)
{
    if (!sc_NtQueryInformationProcess.valid) return false;

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG retLen = 0;

    NTSTATUS status = IndirectSyscall6(
        sc_NtQueryInformationProcess.syscall_addr,
        sc_NtQueryInformationProcess.ssn,
        process,
        (ULONG)ProcessBasicInformation,
        &pbi,
        (ULONG)sizeof(pbi),
        &retLen,
        (PVOID)nullptr);

    if (NT_SUCCESS(status) && exit_code)
        *exit_code = (DWORD)pbi.ExitStatus;

    return NT_SUCCESS(status);
}

NTSTATUS nt::QuerySystemInformation(SYSTEM_INFORMATION_CLASS info_class,
                                    void* buffer, ULONG buffer_length,
                                    ULONG* return_length)
{
    if (!sc_NtQuerySystemInformation.valid)
        return (NTSTATUS)0xC0000008L;

    return IndirectSyscall4(
        sc_NtQuerySystemInformation.syscall_addr,
        sc_NtQuerySystemInformation.ssn,
        (ULONG)info_class,
        buffer,
        buffer_length,
        return_length);
}
