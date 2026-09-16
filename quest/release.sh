#!/usr/bin/env bash
# Build a release package of the Quest port.
#
#   quest/release.sh VERSION        e.g. quest/release.sh 0.1-beta
#
# Requires a clean working tree (tracked files) and a tag quest-vVERSION on
# HEAD, so the engine reports that tag as its version. Builds from scratch in
# builds/quest-release, signs with the release key and writes
#   builds/release/Descent3-Quest-VERSION/      (APK, README, licenses,
#                                                data tools, source archive)
#   builds/release/Descent3-Quest-VERSION.zip
#
# The release key lives in quest/release/ (not in git). It is created on first
# use. Updates must be signed with the same key, so back it up.
set -euo pipefail
D3_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$D3_ROOT/quest/env.sh"
cd "$D3_ROOT"

VERSION="${1:?usage: quest/release.sh VERSION (e.g. 0.1-beta)}"
TAG="quest-v$VERSION"
NAME="Descent3-Quest-$VERSION"
# versionCode: MAJOR*10000 + MINOR*100 + PATCH, from the numeric part.
IFS=. read -r major minor patch <<<"${VERSION%%-*}"
VERSION_CODE=$(( ${major:-0} * 10000 + ${minor:-0} * 100 + ${patch:-0} ))

git diff --quiet HEAD || { echo "working tree has uncommitted changes" >&2; exit 1; }
[[ "$(git rev-parse -q --verify "refs/tags/$TAG^{commit}" || true)" == "$(git rev-parse HEAD)" ]] \
  || { echo "tag $TAG must point at HEAD (git tag -a $TAG)" >&2; exit 1; }

# Release signing key.
KEY_DIR="$D3_ROOT/quest/release"
mkdir -p "$KEY_DIR"
chmod 700 "$KEY_DIR"
if [[ ! -f "$KEY_DIR/release.keystore" ]]; then
  echo "==> creating release signing key in $KEY_DIR"
  pass="$(head -c 24 /dev/urandom | base64 | tr -d '/+=')"
  keytool -genkeypair -keystore "$KEY_DIR/release.keystore" -storetype PKCS12 \
    -storepass "$pass" -keypass "$pass" -alias descent3quest -keyalg RSA -keysize 4096 -validity 20000 \
    -dname "CN=Descent 3 Quest Port" >/dev/null 2>&1
  printf 'KEY_ALIAS=descent3quest\nKEYSTORE_PASS=%s\n' "$pass" > "$KEY_DIR/signing.env"
  chmod 600 "$KEY_DIR/release.keystore" "$KEY_DIR/signing.env"
  echo "    BACK UP quest/release/: updates must be signed with this key."
fi

echo "==> building $NAME (version code $VERSION_CODE)"
BUILD_DIR="$D3_ROOT/builds/quest-release"
rm -rf "$BUILD_DIR"
BUILD_DIR="$BUILD_DIR" "$D3_ROOT/quest/build.sh" > "$D3_ROOT/builds/quest-release.log" 2>&1 \
  || { echo "build failed, see builds/quest-release.log" >&2; exit 1; }
# Signing settings only for this step (the dev build above uses its own key).
(
  set -a
  source "$KEY_DIR/signing.env"
  set +a
  BUILD_DIR="$BUILD_DIR" OUT_DIR="$BUILD_DIR/apk-release" APK_MODE=vr APK_DEBUGGABLE=0 \
    APK_VERSION_NAME="$VERSION" APK_VERSION_CODE="$VERSION_CODE" APK_NAME="$NAME.apk" \
    KEYSTORE="$KEY_DIR/release.keystore" "$D3_ROOT/quest/build_apk.sh"
) >> "$D3_ROOT/builds/quest-release.log" 2>&1 \
  || { echo "signing failed, see builds/quest-release.log" >&2; exit 1; }

echo "==> packaging"
OUT="$D3_ROOT/builds/release/$NAME"
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/tools" "$OUT/source"
cp "$BUILD_DIR/apk-release/$NAME.apk" "$OUT/"
sed "s/@VERSION@/$VERSION/g; /stands for the release version/d" quest/README.md | cat -s > "$OUT/README.md"
cp quest/CHANGELOG.md "$OUT/CHANGELOG.md"
cp LICENSE THIRD_PARTY.md "$OUT/"
cp quest/tools/stage_gamedata.sh quest/tools/extract_cd.py quest/tools/extract_pkg.py "$OUT/tools/"
git archive --format=tar.gz --prefix="$NAME-source/" -o "$OUT/source/$NAME-source.tar.gz" "$TAG"
(cd "$OUT" && find . -type f ! -name SHA256SUMS -printf '%P\n' | sort | xargs sha256sum > SHA256SUMS)
(cd "$D3_ROOT/builds/release" && zip -q -r "$NAME.zip" "$NAME")

echo
"$ANDROID_SDK_ROOT/build-tools/36.0.0/aapt2" dump badging "$OUT/$NAME.apk" | grep -E "^package:|debuggable" || true
"$ANDROID_SDK_ROOT/build-tools/36.0.0/apksigner" verify --print-certs "$OUT/$NAME.apk" | grep -E "Signer #1 certificate (DN|SHA-256)"
echo "engine version: $(strings "$BUILD_DIR/build/libmain.so" | grep -E "^$TAG" | head -1)"
echo "release: builds/release/$NAME.zip ($(du -h "$D3_ROOT/builds/release/$NAME.zip" | cut -f1))"
