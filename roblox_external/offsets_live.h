#pragma once
// Fills the Offsets:: globals at launch: live version first (imtheo, then the
// GitHub mirrors), then the dump for it (imtheo's pinned per-version JSON
// first, then its latest, the Offsetmanager header mirror, or the jina reader
// proxy) - each URL tried over WinHTTP and then System32\curl.exe, because
// Cloudflare drops WinHTTP's TLS hello on imtheo. Falls back to the on-disk
// cache, the shipped offsets_fallback.json, and finally the compiled defaults
// in offsets.h (which match the version the fallback was generated for).
bool RefreshOffsets();

// What the offsets that are loaded NOW were dumped for, and what the client that is
// running NOW reports - "" when no version source answered. RefreshOffsets() logs the
// pair and the menu shows it, because "attached, but every read fails" looks exactly like
// a broken feature, and the one thing that tells them apart is an offset set for another
// version. Both are snapshots taken by RefreshOffsets(); neither changes after that.
const char* OffsetsLoadedVersion();
const char* OffsetsClientVersion();
