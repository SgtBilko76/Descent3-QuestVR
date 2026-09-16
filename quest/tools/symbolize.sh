#!/usr/bin/env bash
# Symbolize an Android crash backtrace for the Quest build.
#
#   adb logcat -d | quest/tools/symbolize.sh     # from the device log
#   quest/tools/symbolize.sh tombstone.txt
#
# Frames in libmain.so / libSDL3.so are resolved against the unstripped
# libraries in the build tree. Stripping keeps addresses, so these match the
# APK as long as the build tree is the one the installed APK was made from
# (the script compares Build IDs and warns otherwise).
set -euo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$D3_ROOT/quest/env.sh"
BUILD_DIR="${BUILD_DIR:-$D3_ROOT/builds/quest}"
BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
declare -A LIBS=(
  [libmain.so]="$BUILD_DIR/build/libmain.so"
  [libSDL3.so]="$BUILD_DIR/_deps/sdl3-build/libSDL3.so"
)

build_id() { "$BIN/llvm-readelf" -n "$1" 2>/dev/null | awk '/Build ID/ {print $3}'; }

input="$(cat "${1:-/dev/stdin}")"
# Only the most recent crash in the input.
crash="$(printf '%s\n' "$input" | awk '/\*\*\* \*\*\* \*\*\*/ {buf=""} {buf = buf $0 "\n"} END {printf "%s", buf}')"
[[ -n "$crash" ]] || crash="$input"

printf '%s\n' "$crash" | grep -E "signal|Abort message|pid:" | sed 's/^.*DEBUG *: //' | head -4
printf '%s\n' "$crash" | grep -E '#[0-9]+ pc [0-9a-f]+' | while read -r line; do
  frame="$(sed -E 's/.*(#[0-9]+) pc ([0-9a-f]+) +([^ ]+).*/\1 \2 \3/' <<<"$line")"
  read -r num pc path <<<"$frame"
  lib="$(basename "$path")"
  local_lib="${LIBS[$lib]:-}"
  if [[ -n "$local_lib" && -f "$local_lib" ]]; then
    want="$(sed -nE 's/.*BuildId: ([0-9a-f]+).*/\1/p' <<<"$line")"
    have="$(build_id "$local_lib")"
    note=""
    [[ -n "$want" && "$want" != "$have" ]] && note="  [BUILD ID MISMATCH: log $want, local $have]"
    sym="$("$BIN/llvm-symbolizer" --obj="$local_lib" --functions=linkage --demangle --relative-address "0x$pc" \
          | paste -sd' ' | sed -E 's/ +$//')"
    printf '%-4s %-12s %s: %s%s\n' "$num" "$pc" "$lib" "$sym" "$note"
  else
    printf '%-4s %-12s %s\n' "$num" "$pc" "$(sed -E 's/.*pc [0-9a-f]+ +//' <<<"$line")"
  fi
done
