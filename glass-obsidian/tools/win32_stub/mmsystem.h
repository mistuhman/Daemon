// -----------------------------------------------------------------------------
//  Minimal stand-in for <mmsystem.h>, for tools/check_win_tu.sh only. See the
//  header of the stub <Windows.h> for why these exist; the app's Win32 glue
//  (platform_win.cpp) is the only code that includes them.
// -----------------------------------------------------------------------------
#pragma once
#include <Windows.h>
typedef unsigned int MMRESULT;
#define SND_FILENAME 0x00020000
#define SND_ASYNC 0x0001
#define SND_NODEFAULT 0x0002
inline BOOL PlaySoundA(LPCSTR, HMODULE, DWORD) { return FALSE; }
inline MMRESULT waveOutSetVolume(HANDLE, DWORD) { return 0; }
