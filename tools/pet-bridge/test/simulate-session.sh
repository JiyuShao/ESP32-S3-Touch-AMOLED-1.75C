#!/bin/sh
# End-to-end protocol check against a live bridge:
# priority resolution, idle-vs-active TTL split, sleeping → session_deleted.
# Exits non-zero on failure.
cd "$(dirname "$0")/.." || exit 1

LOG=$(mktemp) || exit 1
# short TTLs so the expiry steps run in seconds, not minutes
PET_BRIDGE_SESSION_TTL_MS=1200 PET_BRIDGE_ACTIVE_TTL_MS=4000 node bridge.js > "$LOG" 2>&1 &
BRIDGE_PID=$!
trap 'kill $BRIDGE_PID 2>/dev/null; rm -f "$LOG"' EXIT
sleep 0.5

post() { # post $1 = state, $2 = session_id
  curl -s -o /dev/null -X POST http://127.0.0.1:8787/state \
    -H 'Content-Type: application/json' \
    -d "{\"state\":\"$1\",\"session_id\":\"$2\"}"
}

code=$(curl -s -o /dev/null -w '%{http_code}' -X POST http://127.0.0.1:8787/state \
  -H 'Content-Type: application/json' -d '{"state":"jugglingX","session_id":"z"}')
[ "$code" = "400" ] || { echo "FAIL: invalid state got HTTP $code"; exit 1; }

# active TTL exemption: working x survives past the idle TTL (1.2 s)…
post working x
sleep 1.6
post idle y          # triggers sweep; x must still be alive
grep -q 'session expired: x' "$LOG" && { echo 'FAIL: working session expired at idle TTL'; exit 1; }

# …but not past the active TTL (4 s)
sleep 2.5
post idle z          # sweep: x is stale now
sleep 0.2
grep -q 'session expired: x' "$LOG" || { echo 'FAIL: active session never expired'; sed 's/^/  log: /' "$LOG"; exit 1; }

post thinking a      # → broadcast thinking
post working b       # → working dominates
post error a         # → error dominates
post thinking b      # → still error (no broadcast)
post idle a          # → thinking (b) dominates again

# sleeping removes the session from the list immediately
post sleeping b

# idle TTL: a's idle expires
sleep 1.6
post idle c          # sweep: a expired, c stays

sleep 0.4
kill $BRIDGE_PID 2>/dev/null
wait $BRIDGE_PID 2>/dev/null
trap - EXIT

check() { # $1 = text the log must contain
  grep -q "$1" "$LOG" || { echo "FAIL: missing '$1'"; sed 's/^/  log: /' "$LOG"; exit 1; }
}

check 'state: thinking'
check 'state: working'
check 'state: error'
check 'session expired: x'
check 'session ended: b'
check 'session expired: a'
check 'state: idle'
rm -f "$LOG"
echo 'PASS: protocol end-to-end (priority, TTL split, session_deleted)'
