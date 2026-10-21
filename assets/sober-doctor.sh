#!/bin/sh
# sober-doctor — read Sober's own client log and say what actually killed it.
# Run anytime:  sh <payload>/assets/sober-doctor.sh     (installed as ~/.local/bin/sober-doctor)
#
# Why this exists (ti-16): the operator's "Sober crashes whenever daemon is
# loaded" turned out, in the log, to be a CLEAN Android teardown — the client
# was healthy (139 fps) and something ASKED it to close; no RBXCRASH, no
# freeze. The kill was traced to an asset-intercepting proxy that terminates
# Roblox when its proxy starts (Fleasion "Clear Cache on Launch", default on).
# This script turns that manual forensics into one command for any operator:
# it finds the log for ANY install style, then classifies the death.
#
# Read-only. No curl, no network, no writes.

LABEL="[sober-doctor]"

# ---- 1. find the client log (install-dependent) -----------------------------
# Flatpak keeps XDG data under ~/.var/app; a standalone/bare Sober keeps it
# under ~/.local/share (or legacy ~/.sober). The file appears/rotates per
# session — if nothing is readable yet, run this again right after the next
# close; also check previous.log beside it.
LOG=""
for CAND in \
    "$HOME/.var/app/org.vinegarhq.Sober/data/sober/appData/logs/latest.log" \
    "$HOME/.local/share/sober/appData/logs/latest.log" \
    "$HOME/.sober/appData/logs/latest.log"
do
    if [ -r "$CAND" ]; then LOG="$CAND"; break; fi
done
if [ -z "$LOG" ]; then
    LOG=$(find "$HOME/.var/app/org.vinegarhq.Sober" "$HOME/.local/share/sober" \
               "$HOME/.sober" -name latest.log -path '*appData/logs*' 2>/dev/null | head -n 1)
fi

if [ -z "$LOG" ]; then
    echo "$LABEL no readable Sober client log found under the known roots:"
    echo "$LABEL   ~/.var/app/org.vinegarhq.Sober/data/sober/appData/logs (flatpak)"
    echo "$LABEL   ~/.local/share/sober/appData/logs (standalone)"
    echo "$LABEL the log is written per session — run this again right after the next close,"
    echo "$LABEL or point me at the file: sober-doctor /path/to/log"
    [ "$#" -ge 1 ] && [ -r "$1" ] && LOG="$1"
fi
if [ -z "$LOG" ]; then exit 0; fi
echo "$LABEL Sober client log: $LOG"

# ---- 2. name the death ------------------------------------------------------
# The client's error screen (RBXCRASH) quotes the reason into this same log.
if grep -aq "RBXCRASH" "$LOG"; then
    echo "$LABEL last RBXCRASH lines (what the error screen quoted):"
    grep -a "RBXCRASH" "$LOG" | tail -n 3 | sed "s/^/$LABEL   /"
    if grep -aq "OutOfMemory" "$LOG"; then
        echo "$LABEL RBXCRASH OutOfMemory — VRAM exhaustion; documented fix (VinegarHQ troubleshooting):"
        echo "$LABEL   in ~/.var/app/org.vinegarhq.Sober/config/sober/config.json, \"fflags\" section, add:"
        echo "$LABEL     \"DFIntTextureQualityOverride\": 2, \"DFFlagTextureQualityOverrideEnabled\": true"
        echo "$LABEL   (use 1 instead of 2 if 2 still crashes)"
    fi
else
    echo "$LABEL no RBXCRASH lines — the client never hit its error screen."
fi

# ---- 3. clean teardown = something asked Sober to close ---------------------
# destroyLuaApp/setStage at the tail means the app layer ran the full Android
# destroy sequence: an external close request (window close, session end, or a
# terminator). A graphics crash or freeze NEVER produces this tail.
if tail -n 15 "$LOG" | grep -aq "destroyLuaApp\|setStage\|nativeAppBridgeV2LeaveGame"; then
    echo "$LABEL VERDICT: the log ends in a CLEAN teardown (leaveGame/destroyLuaApp/"
    echo "$LABEL setStage: None) — the client was healthy and something ASKED it to close."
    echo "$LABEL That is not a graphics crash. Prime suspects, in order:"
    echo "$LABEL   - an asset proxy terminating Roblox when its proxy starts"
    echo "$LABEL     (e.g. Fleasion: Settings > 'Clear Cache on Launch' is ON by default"
    echo "$LABEL      and KILLS a running Roblox when the proxy connects)"
    echo "$LABEL   - the window manager / session ending the window"
    echo "$LABEL   - you closed it :)"
fi

# ---- 4. intercepting-proxy fingerprint --------------------------------------
# TLS hostname mismatches on roblox.com endpoints mean something is MITM-ing
# the session's HTTPS. Daemon does not touch Sober's network — this is always
# external (proxy/VPN/filter). Broken avatar thumbnails ride along with it.
if grep -aq "TlsVerificationFail\|no alternative certificate subject name" "$LOG"; then
    N=$(grep -ac "TlsVerificationFail" "$LOG")
    echo "$LABEL $N TLS verification failures against roblox.com — an intercepting proxy is"
    echo "$LABEL in this session (external to daemon). If you run one (Fleasion, a VPN filter,"
    echo "$LABEL corporate MITM), its terminate/relaunch settings are part of your crash story."
fi

# ---- 5. known telemetry quirks ----------------------------------------------
# Sober reports video memory as 64MB on some GPUs regardless of the real card
# (vinegarhq/sober issues #2208, #2165). Worth knowing; not a death sentence —
# the log's own fps telemetry is the counter-evidence.
if grep -aq "gpu_memMB=64\|gpu%5FmemMB=64" "$LOG"; then
    echo "$LABEL client telemetry reports gpu_memMB=64 — the known Sober VRAM-reporting clamp"
    echo "$LABEL (vinegarhq/sober #2208, #2165). Check the log's averageFPS before blaming it."
fi

# ---- 6. fps + join context (was the session even unhealthy?) -----------------
grep -ao "averageFPS=[0-9.]*" "$LOG" | tail -n 1 | sed "s/^/$LABEL telemetry: /"
grep -a "Joining game\|Join snapshot" "$LOG" | tail -n 1 | sed "s/^/$LABEL /"

exit 0
