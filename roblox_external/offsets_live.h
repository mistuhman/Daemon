#pragma once
// Fills the Offsets:: globals at launch: live version first (imtheo, then the
// GitHub mirrors), then the dump for it (imtheo's pinned per-version JSON
// first, then its latest, the Offsetmanager header mirror, or the jina reader
// proxy) - each URL tried over WinHTTP and then System32\curl.exe, because
// Cloudflare drops WinHTTP's TLS hello on imtheo. Falls back to the on-disk
// cache, the shipped offsets_fallback.json, and finally the compiled defaults
// in offsets.h (which match the version the fallback was generated for).
bool RefreshOffsets();
