#!/bin/sh
# diff_core.sh — detect drift between Notion Classic's and Claude 68's src/core.
#
# Both apps carry a REAL COPY of src/core; fixes must be ported manually in
# both directions. Run this after touching anything in src/core.
#
# Usage: scripts/diff_core.sh [path to Claude 68]   (default: ../Claude68)

A="$(cd "$(dirname "$0")/.." && pwd)"
B="${1:-$A/../Claude68}"

if [ ! -d "$B/src/core" ]; then
    echo "diff_core: sibling repo not found at $B" >&2
    exit 2
fi

# Exclusions:
#   lib/            prebuilt binaries (identical by construction, copied)
#   .DS_Store       Finder noise
#   sse.cpp, api_client.h        Claude 68 only (SSE/chat contract)
#   emoji_*, utf8_mac_lite.h     Notion Classic only (page rendering)
#   tls_cache.h                  Claude 68 only
diff -rq "$A/src/core" "$B/src/core" \
    -x lib -x .DS_Store \
    -x sse.cpp -x api_client.h -x tls_cache.h \
    -x 'emoji_*' -x utf8_mac_lite.h
rc=$?

if [ $rc -eq 0 ]; then
    echo "core in sync."
else
    echo ""
    echo "^ core drift detected — port fixes in BOTH directions or confirm the"
    echo "  difference is intentionally app-specific (then add an -x exclusion)."
fi
exit $rc
