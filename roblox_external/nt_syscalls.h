#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  nt_syscalls.h — indirect syscalls through unhooked ntdll.dll
//
//  All anti-cheat-detectable WinAPI calls (OpenProcess, ReadProcessMemory,
//  WriteProcessMemory, VirtualAllocEx, etc.) are replaced with their NT
//  equivalents, dispatched through indirect syscalls that jump to the original
//  `syscall; ret` inside ntdll.dll rather than executing syscalls from our
//  own memory.
//
//  Initialization:
//    1. NTDLL unhooking — maps a fresh ntdll from disk and overwrites the
//       .text section of the loaded copy (removes Byfron hooks).
//    2. SSN resolution  — parses ntdll exports, extracts syscall numbers
//       and finds each function's `syscall; ret` address.
//
//  After Init(), the nt::* wrappers are drop-in replacements for the
//  corresponding WinAPI calls.
// ─────────────────────────────────────────────────────────────────────────────

#include <Windows.h>
#include <cstdint>
#include <cstddef>

// ── NT status codes ──────────────────────────────────────────────────────────
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)
#endif

// ── NT types ─────────────────────────────────────────────────────────────────
typedef LONG NTSTATUS;
typedef ULONG ACCESS_MASK;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        PVOID    Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _PROCESS_BASIC_INFORMATION {
    NTSTATUS    ExitStatus;
    PVOID       PebBaseAddress;
    ULONG_PTR   AffinityMask;
    LONG        BasePriority;
    HANDLE      UniqueProcessId;
    HANDLE      InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

// ── Process access rights (subset we need) ───────────────────────────────────
#ifndef PROCESS_VM_READ
#define PROCESS_VM_READ            0x0010
#define PROCESS_VM_WRITE           0x0020
#define PROCESS_VM_OPERATION       0x0008
#define PROCESS_QUERY_INFORMATION  0x0400
#endif

// ── Memory protection ────────────────────────────────────────────────────────
#ifndef PAGE_READWRITE
#define PAGE_READWRITE    0x04
#define MEM_COMMIT        0x1000
#define MEM_RESERVE       0x2000
#define MEM_RELEASE       0x8000
#endif

// ── System information classes ───────────────────────────────────────────────
typedef enum _SYSTEM_INFORMATION_CLASS {
    SystemProcessInformation = 5
} SYSTEM_INFORMATION_CLASS;

typedef enum _PROCESSINFOCLASS {
    ProcessBasicInformation = 0,
    ProcessWow64Information = 26
} PROCESSINFOCLASS;

// ── System process entry (for NtQuerySystemInformation) ──────────────────────
typedef struct _SYSTEM_PROCESS_INFORMATION {
    ULONG       NextEntryOffset;
    ULONG       NumberOfThreads;
    LARGE_INTEGER SpareLi1;
    LARGE_INTEGER SpareLi2;
    LARGE_INTEGER SpareLi3;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    LONG          BasePriority;
    HANDLE        UniqueProcessId;
    HANDLE        InheritedFromUniqueProcessId;
    ULONG         HandleCount;
    ULONG         SessionId;
    ULONG_PTR     PageDirectoryBase;
    SIZE_T        PeakVirtualSize;
    SIZE_T        VirtualSize;
    ULONG         PageFaultCount;
    SIZE_T        PeakWorkingSetSize;
    SIZE_T        WorkingSetSize;
    SIZE_T        QuotaPeakPagedPoolUsage;
    SIZE_T        QuotaPagedPoolUsage;
    SIZE_T        QuotaPeakNonPagedPoolUsage;
    SIZE_T        QuotaNonPagedPoolUsage;
    SIZE_T        PagefileUsage;
    SIZE_T        PeakPagefileUsage;
    SIZE_T        PrivatePageCount;
    IO_STATUS_BLOCK IoCounters[6];  // simplified
    // Thread array follows (we don't need it)
} SYSTEM_PROCESS_INFORMATION, *PSYSTEM_PROCESS_INFORMATION;


// ── Syscall table entry ──────────────────────────────────────────────────────
struct SyscallEntry {
    DWORD  ssn;             // system service number
    void*  syscall_addr;    // address of `syscall; ret` (0F 05 C3) in ntdll
    bool   valid;
};


// ── Initialization ───────────────────────────────────────────────────────────
namespace nt {

    // Must be called once at startup.  Unhooks ntdll, resolves SSNs.
    // Returns true on success.
    bool Init();

    // ── NT API wrappers (drop-in replacements) ──────────────────────────────

    // OpenProcess → NtOpenProcess
    bool OpenProcess(DWORD pid, DWORD access, HANDLE* out_handle);

    // ReadProcessMemory → NtReadVirtualMemory
    bool ReadProcessMemory(HANDLE process, uint64_t address,
                           void* buffer, size_t size, size_t* bytes_read);

    // WriteProcessMemory → NtWriteVirtualMemory
    bool WriteProcessMemory(HANDLE process, uint64_t address,
                            const void* data, size_t size, size_t* bytes_written);

    // VirtualAllocEx → NtAllocateVirtualMemory
    bool AllocateVirtualMemory(HANDLE process, void** base_address,
                               size_t size, DWORD allocation_type,
                               DWORD protect);

    // VirtualFreeEx → NtFreeVirtualMemory
    bool FreeVirtualMemory(HANDLE process, void** base_address,
                           size_t size, DWORD free_type);

    // CloseHandle → NtClose
    bool CloseHandle(HANDLE handle);

    // GetExitCodeProcess → NtQueryInformationProcess
    bool GetProcessExitStatus(HANDLE process, DWORD* exit_code);

    // NtQuerySystemInformation wrapper
    NTSTATUS QuerySystemInformation(SYSTEM_INFORMATION_CLASS info_class,
                                    void* buffer, ULONG buffer_length,
                                    ULONG* return_length);

    // ── Syscall entries (exposed for advanced use) ──────────────────────────
    extern SyscallEntry sc_NtOpenProcess;
    extern SyscallEntry sc_NtReadVirtualMemory;
    extern SyscallEntry sc_NtWriteVirtualMemory;
    extern SyscallEntry sc_NtAllocateVirtualMemory;
    extern SyscallEntry sc_NtFreeVirtualMemory;
    extern SyscallEntry sc_NtClose;
    extern SyscallEntry sc_NtQueryInformationProcess;
    extern SyscallEntry sc_NtQuerySystemInformation;
    extern SyscallEntry sc_NtQueryVirtualMemory;
    extern SyscallEntry sc_NtProtectVirtualMemory;
}

// ── ASM stubs (defined in syscall_stub.asm) ─────────────────────────────────
extern "C" {
    NTSTATUS IndirectSyscall(void* syscall_addr, DWORD ssn, ...);
    NTSTATUS IndirectSyscall4(void* syscall_addr, DWORD ssn, ...);
    NTSTATUS IndirectSyscall6(void* syscall_addr, DWORD ssn, ...);
}
