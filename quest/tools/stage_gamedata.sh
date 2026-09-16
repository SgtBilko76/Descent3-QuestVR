#!/usr/bin/env bash
# Turn a Descent 3 CD image into the data folder the Quest build reads, and
# optionally push it to the headset.
#
#   stage_gamedata.sh IMAGE [--push]
#
# Unpacks the disc (extract_cd.py) and its installer archives (extract_pkg.py),
# then keeps only what the engine loads: *.hog, missions/, movies/. The 1999
# Windows binaries (exe/dll, netgames/*.d3m, online/*.d3c, editor) are left
# out; the port builds its own arm64 versions of the modules.
set -euo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE="${1:?usage: stage_gamedata.sh IMAGE [--push]}"
WORK="${WORK_DIR:-$D3_ROOT/builds/gamedata-work}"
STAGE="${STAGE_DIR:-$D3_ROOT/builds/gamedata}"
DEVICE_DIR="/sdcard/Android/data/com.descentdevelopers.descent3/files"

rm -rf "$WORK" "$STAGE"
python3 "$D3_ROOT/quest/tools/extract_cd.py" "$IMAGE" "$WORK/cd"

# Base archives first, then patches, so newer files win. The *_130 patch
# archives differ only in executables; the data they carry is identical, so
# the English one is enough.
shopt -s nullglob nocaseglob
pkgs=()
for p in "$WORK"/cd/*.pkg; do
  case "$(basename "$p" | tr 'A-Z' 'a-z')" in
    *_1[0-9][0-9].pkg) ;;          # patches, added below
    *) pkgs+=("$p") ;;
  esac
done
for p in "$WORK"/cd/eng_*.pkg; do pkgs+=("$p"); done
[[ ${#pkgs[@]} -gt 0 ]] || { echo "no .pkg archives on this disc" >&2; exit 1; }
python3 "$D3_ROOT/quest/tools/extract_pkg.py" "$WORK/pkg" "${pkgs[@]}"

mkdir -p "$STAGE"
cp -p "$WORK"/pkg/*.hog "$STAGE"/ 2>/dev/null || true
for d in missions movies; do
  [[ -d "$WORK/pkg/$d" ]] && cp -rp "$WORK/pkg/$d" "$STAGE/"
done
# Also accept discs that ship loose files instead of archives.
cp -p "$WORK"/cd/*.hog "$STAGE"/ 2>/dev/null || true
for d in missions movies; do
  [[ -d "$WORK/cd/$d" ]] && cp -rp "$WORK/cd/$d" "$STAGE/"
done
shopt -u nocaseglob

echo "staged in $STAGE:"
(cd "$STAGE" && find . -type f -printf '  %10s  %P\n' | sort -k2)

if ! ls "$STAGE" | grep -qix 'd3.hog'; then
  echo
  echo "WARNING: no d3.hog. This disc does not contain the base game (the"
  echo "Mercenary disc is an expansion). The engine cannot start without d3.hog"
  echo "from a Descent 3 install; add it to $STAGE before pushing."
fi

if [[ "${2:-}" == "--push" ]]; then
  source "$D3_ROOT/quest/env.sh"
  # Create every directory first and push into it: letting adb create
  # directories under /sdcard/Android/data intermittently fails with
  # "secure_mkdirs failed: Operation not permitted".
  adb shell mkdir -p "$DEVICE_DIR"
  (cd "$STAGE" && find . -mindepth 1 -type d -printf '%P\n') | while read -r d; do
    adb shell mkdir -p "\"$DEVICE_DIR/$d\"" </dev/null
  done
  (cd "$STAGE" && find . -maxdepth 1 -type f -printf '%P\n') | while read -r f; do
    adb push "$STAGE/$f" "$DEVICE_DIR/$f" </dev/null
  done
  (cd "$STAGE" && find . -mindepth 1 -maxdepth 1 -type d -printf '%P\n') | while read -r d; do
    adb push "$STAGE/$d/." "$DEVICE_DIR/$d/" </dev/null
  done
fi
