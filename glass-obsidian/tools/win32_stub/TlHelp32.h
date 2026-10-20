// Stub <TlHelp32.h>: just enough for a syntax pass. See Windows.h in this folder.
#pragma once
#include <Windows.h>
typedef struct { DWORD dwSize; DWORD th32ProcessID; } PROCESSENTRY32;
typedef struct { DWORD dwSize; HMODULE hModule; char szExeFile[MAX_PATH]; } MODULEENTRY32;
#define TH32CS_SNAPPROCESS 2
#define TH32CS_SNAPMODULE 8
inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return nullptr; }
