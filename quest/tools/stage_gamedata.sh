#!/usr/bin/env bash
# Turn Descent 3 CD images into the data folder the Quest build reads, and
# optionally push it to the headset.
#
#   stage_gamedata.sh IMAGE [IMAGE ...] [--push]
#   stage_gamedata.sh --push-only      # push an existing builds/gamedata
#
# Give the discs in install order: base game first, then expansions, e.g.
#   stage_gamedata.sh Descent3_CD1.iso Descent3_CD2.iso "Descent 3 Mercenary.iso" --push
# Later images override files from earlier ones.
#
# For each image: extract the disc (extract_cd.py), take its loose game files,
# then unpack its installer archives: InstallShield cabinets (via unshield,
# built on demand) and Outrage "GKPO" packages (extract_pkg.py). Only what
# the engine loads is kept: *.hog, *.pld, missions/, movies/, demo/, custom/.
# The 1999 Windows binaries (exe/dll, netgames/*.d3m, online/*.d3c, editor)
# are left out; the port builds its own arm64 versions of the modules.
set -euo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${WORK_DIR:-$D3_ROOT/builds/gamedata-work}"
STAGE="${STAGE_DIR:-$D3_ROOT/builds/gamedata}"
DEVICE_DIR="/sdcard/Android/data/com.descentdevelopers.descent3/files"
KEEP_DIRS=(missions movies demo custom)

push=0
push_only=0
images=()
for arg in "$@"; do
  case "$arg" in
    --push) push=1 ;;
    --push-only) push=1; push_only=1 ;;
    *) images+=("$arg") ;;
  esac
done
if [[ $push_only -eq 0 && ${#images[@]} -eq 0 ]]; then
  echo "usage: stage_gamedata.sh IMAGE [IMAGE ...] [--push] | --push-only" >&2
  exit 1
fi

source "$D3_ROOT/quest/env.sh"
shopt -s nullglob nocaseglob

UNSHIELD="${UNSHIELD:-}"
need_unshield() {
  [[ -n "$UNSHIELD" ]] && return
  if command -v unshield >/dev/null; then UNSHIELD="$(command -v unshield)"; return; fi
  local src="$D3_ROOT/builds/unshield-src" bld="$D3_ROOT/builds/unshield-build"
  if [[ ! -x "$bld/src/unshield" ]]; then
    echo "==> building unshield (InstallShield extractor)"
    [[ -d "$src" ]] || git clone -q --depth 1 --branch 1.6.2 https://github.com/twogood/unshield.git "$src"
    # The bundled MD5 code is K&R C, which C23 compilers (GCC 15+) reject.
    cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_STATIC=ON \
      -DUSE_OUR_OWN_MD5=ON -DCMAKE_C_FLAGS=-std=gnu17 >/dev/null
    cmake --build "$bld" >/dev/null
  fi
  UNSHIELD="$bld/src/unshield"
}

# Copy the game-data subset of a directory tree into $STAGE (later calls win).
take_game_files() {
  local from="$1"
  for f in "$from"/*.hog "$from"/*.pld; do cp -p "$f" "$STAGE/"; done
  for d in "${KEEP_DIRS[@]}"; do
    for match in "$from"/"$d"; do
      [[ -d "$match" ]] || continue
      mkdir -p "$STAGE/$d"
      cp -rp "$match"/. "$STAGE/$d/"
    done
  done
}

if [[ $push_only -eq 1 ]]; then
  [[ -f "$STAGE/d3.hog" ]] || { echo "nothing staged in $STAGE (or no d3.hog)" >&2; exit 1; }
  images=()
else
rm -rf "$WORK" "$STAGE"
mkdir -p "$STAGE"
fi
n=0
for image in ${images[@]+"${images[@]}"}; do
  n=$((n + 1))
  disc="$WORK/disc$n"
  echo "==> $image"
  python3 "$D3_ROOT/quest/tools/extract_cd.py" "$image" "$disc"
  take_game_files "$disc"

  # InstallShield cabinets. A volume set is extracted from its first volume;
  # anything named .cab that is not a real archive is reported and skipped
  # (some disc copies carry filler files under archive names).
  for cab in "$disc"/*.cab; do
    magic="$(head -c 4 "$cab" | od -An -tx1 | tr -d ' \n')"
    case "$magic" in
      49536328)  # "ISc("
        name="$(basename "$cab" | tr 'A-Z' 'a-z')"
        [[ "$name" =~ ^data1\.cab$|^[a-z]+1\.cab$ ]] || continue
        [[ "$name" == _* ]] && continue   # _sys1/_user1: installer internals
        need_unshield
        out="$WORK/is$n-${name%.cab}"
        "$UNSHIELD" -d "$out" x "$cab" >/dev/null
        for group in "$out"/*/; do take_game_files "$group"; done
        ;;
      4d534346)  # "MSCF": Microsoft cabinet; not seen on Descent 3 discs yet
        echo "    skipping $(basename "$cab"): Microsoft CAB extraction is not implemented" >&2
        ;;
      *)
        echo "    WARNING: $(basename "$cab") is not an archive (no known header); skipped." >&2
        if [[ "$(head -c 65536 "$cab" | od -An -tx1 | tr -s ' \n' ' ' | tr ' ' '\n' | sort -u | grep -c .)" -le 2 ]]; then
          echo "             Its content is a repeating filler pattern, so this copy of the disc lacks that data." >&2
        fi
        ;;
    esac
  done

  # Outrage installer packages: base archives first, then patches, so newer
  # files win. The *_130 patch archives differ only in executables; the data
  # they carry is identical, so the English one is enough.
  pkgs=()
  for p in "$disc"/*.pkg; do
    case "$(basename "$p" | tr 'A-Z' 'a-z')" in
      *_1[0-9][0-9].pkg) ;;
      *) pkgs+=("$p") ;;
    esac
  done
  for p in "$disc"/eng_*.pkg; do pkgs+=("$p"); done
  if [[ ${#pkgs[@]} -gt 0 ]]; then
    python3 "$D3_ROOT/quest/tools/extract_pkg.py" "$WORK/pkg$n" "${pkgs[@]}"
    take_game_files "$WORK/pkg$n"
  fi
done
shopt -u nocaseglob

# USAGE.md: the engine expects a custom/ folder.
mkdir -p "$STAGE/custom"

echo
echo "staged in $STAGE:"
(cd "$STAGE" && find . -type f -printf '  %10s  %P\n' | sort -k2)
echo "  total: $(du -sh "$STAGE" | cut -f1)"

if ! ls "$STAGE" | grep -qix 'd3.hog'; then
  echo
  echo "WARNING: no d3.hog. None of these discs contain the base game (the"
  echo "Mercenary disc is an expansion). The engine cannot start without d3.hog."
fi

if [[ $push -eq 1 ]]; then
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
    [[ -n "$(ls -A "$STAGE/$d")" ]] || continue
    adb push "$STAGE/$d/." "$DEVICE_DIR/$d/" </dev/null
  done
  # adb creates everything as the shell user with mode 2770/0644, and the app
  # is neither that user nor in the group, so it could not enter the
  # subdirectories. Open them up (custom/ must also be writable). This does not
  # expose anything: files/ itself stays app-owned and closed to other users.
  adb shell "cd \"$DEVICE_DIR\" && find . -mindepth 1 -type d -exec chmod 2777 {} + && find . -type f -exec chmod 0644 {} +"
fi
