; ==========================================================================
;  syscall_stub.asm — indirect syscall dispatcher (x64 MASM)
;
;  Jumps to the `syscall; ret` pair inside ntdll.dll instead of executing
;  a syscall from the external's own memory.  The execution trace looks
;  like a legitimate ntdll call to Hyperion/Byfron.
;
;  All three stubs share the same C++ calling convention:
;    rcx = syscall_addr   (pointer to 0F 05 C3 in ntdll)
;    rdx = SSN            (system service number)
;    r8  = NT arg1
;    r9  = NT arg2
;    [rsp+28h] = NT arg3
;    [rsp+30h] = NT arg4
;    [rsp+38h] = NT arg5   (IndirectSyscall6 only)
;    [rsp+40h] = NT arg6   (IndirectSyscall6 only)
;
;  After the shuffle:
;    r10 = NT arg1, rcx = NT arg1, rdx = NT arg2
;    r8 = NT arg3, r9 = NT arg4
;    [rsp+28h] = NT arg5, [rsp+30h] = NT arg6
;    eax = SSN
;    jmp to syscall; ret in ntdll
; ==========================================================================

.code

; ──────────────────────────────────────────────────────────────────────────
;  1 NT arg (NtClose)
;  Simple case: no register conflicts.
; ──────────────────────────────────────────────────────────────────────────
IndirectSyscall PROC
    mov     r11, rcx                ; r11 = syscall_addr
    mov     r10, r8                 ; r10 = NT arg1 (kernel convention)
    mov     rcx, r8                 ; rcx = NT arg1
    mov     eax, edx                ; eax = SSN
    jmp     r11
IndirectSyscall ENDP


; ──────────────────────────────────────────────────────────────────────────
;  4 NT args (NtOpenProcess, NtFreeVirtualMemory, NtQuerySystemInformation)
;  Save SSN before clobbering rdx, then shuffle freely.
; ──────────────────────────────────────────────────────────────────────────
IndirectSyscall4 PROC
    mov     [rsp + 8], edx          ; save SSN to shadow space
    mov     r11, rcx                ; r11 = syscall_addr
    mov     r10, r8                 ; r10 = NT arg1
    mov     rcx, r8                 ; rcx = NT arg1
    mov     rdx, r9                 ; rdx = NT arg2
    mov     r8,  [rsp + 28h]       ; r8  = NT arg3
    mov     r9,  [rsp + 30h]       ; r9  = NT arg4
    mov     eax, [rsp + 8]         ; restore SSN
    jmp     r11
IndirectSyscall4 ENDP


; ──────────────────────────────────────────────────────────────────────────
;  5-6 NT args (NtReadVirtualMemory, NtWriteVirtualMemory,
;               NtQueryInformationProcess, NtAllocateVirtualMemory)
;
;  Stack shuffle: NT arg3/4 move from [rsp+28h/30h] to r8/r9, then
;  NT arg5/6 move from [rsp+38h/40h] into the freed [rsp+28h/30h].
;  SSN is saved to [rsp+8] (shadow space) first, restored to eax last.
; ──────────────────────────────────────────────────────────────────────────
IndirectSyscall6 PROC
    ; ── Save SSN to shadow space ──
    mov     [rsp + 8], edx

    ; ── Save syscall_addr ──
    mov     r11, rcx

    ; ── Move NT arg1 to r10 + rcx ──
    mov     r10, r8
    mov     rcx, r8

    ; ── Move NT arg2 to rdx (before r9 is overwritten) ──
    mov     rdx, r9

    ; ── Read NT arg3/4 from stack into r8/r9 ──
    mov     r8,  [rsp + 28h]
    mov     r9,  [rsp + 30h]

    ; ── Place NT arg5/6 at kernel-expected stack positions ──
    mov     rax, [rsp + 38h]
    mov     [rsp + 28h], rax
    mov     rax, [rsp + 40h]
    mov     [rsp + 30h], rax

    ; ── Restore SSN to eax ──
    mov     eax, [rsp + 8]

    jmp     r11
IndirectSyscall6 ENDP

END
