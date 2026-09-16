#!/usr/bin/env bash
# Start the installed Quest build directly in a level (no intro, cutscene or
# briefing), wait until it renders, then report frame statistics.
#
#   play_level.sh [LEVEL] [SECONDS] [extra engine args...]
#
# Uses the "Quest" pilot and the main campaign (d3.mn3). Engine logs of the run
# are written to builds/play_level.log.
set -uo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$D3_ROOT/quest/env.sh"
LEVEL="${1:-1}"; SECONDS_TO_RUN="${2:-20}"; shift $(( $# > 2 ? 2 : $# )) || true
PKG=com.descentdevelopers.descent3
LOG="$D3_ROOT/builds/play_level.log"

adb shell am force-stop "$PKG"
adb logcat -c
adb shell am start -n "$PKG/.D3Activity" \
  --es args "\"-nointro -skipbriefings -pilot Quest -mission d3 -loadlevel $LEVEL $*\"" >/dev/null
( timeout $((SECONDS_TO_RUN + 40)) adb logcat -v time > "$LOG" 2>&1 || true ) &
logger_pid=$!
timeout 2 adb logcat -T 1 >/dev/null 2>&1 || true

if grep -q "launch_blocked_controller_required" "$LOG" 2>/dev/null; then
  echo "note: Horizon is asking for the Touch controllers before it starts the app" >&2
fi

# Wait until the level is being rendered.
for _ in $(seq 90); do
  grep -q "stereo)" "$LOG" 2>/dev/null && ! grep -q "(0% stereo)" <(grep "stereo)" "$LOG" | tail -1) && break
  timeout 1 adb logcat -T 1 >/dev/null 2>&1 || true
done
timeout "$SECONDS_TO_RUN" adb logcat -T 1 >/dev/null 2>&1 || true
kill "$logger_pid" 2>/dev/null || true
wait "$logger_pid" 2>/dev/null || true

echo "pid: $(adb shell pidof $PKG | tr -d '\r')"
grep -E "Descent3.*(VR:|ERROR)|libc\+\+abi|F/DEBUG" "$LOG" | grep -vE "Osiris_Bind|ret_pr0|GetBasePath" \
  | sed -E 's/Descent3\([0-9]+\): [0-9-]+ [0-9:.]+ //' | tail -8
echo "--- runtime (VrApi) ---"
grep -E "VrApi.*FPS=" "$LOG" | tail -4 | sed -E 's/.*(FPS=[^,]+).*(CPU4\/GPU=[^,]+).*(GPU%=[^,]+).*(CPU%=[^(,]+).*/\1 \2 \3 \4/'
