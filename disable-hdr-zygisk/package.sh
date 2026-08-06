#!/usr/bin/env bash
# Builds the native module + hook dex and assembles the flashable zip.
# Usage: ./package.sh
set -euo pipefail
cd "$(dirname "$0")"

echo "== Building native module (all ABIs) =="
./gradlew :native:externalNativeBuildRelease

echo "== Building hook dex (Bridge.java) =="
./gradlew :hookdex:extractHookDex

MODULE_ID="disable_hdr"
OUT="dist/$MODULE_ID"
rm -rf "$OUT" "dist/$MODULE_ID.zip"
mkdir -p "$OUT/zygisk"

echo "== Copying module skeleton =="
cp -r flashable_module/. "$OUT/"

echo "== Copying native .so per ABI =="
# AGP's CMake integration (unlike the old ndk-build path) puts outputs under
# build/intermediates/cmake/<variant>/obj/<ABI>/ - not obj/local/<ABI>/.
NATIVE_LIBS_DIR="native/build/intermediates/cmake/release/obj"
missing=0
for abi in arm64-v8a armeabi-v7a x86 x86_64; do
  src="$NATIVE_LIBS_DIR/$abi/libdisable_hdr.so"
  if [ -f "$src" ]; then
    cp "$src" "$OUT/zygisk/$abi.so"
    echo "  copied $abi.so"
  else
    echo "  WARNING: missing build output for $abi ($src)"
    missing=1
  fi
done
if [ "$missing" = 1 ]; then
  echo "!! One or more ABIs failed to build - check the externalNativeBuildRelease output above." >&2
fi

echo "== Copying classes.dex =="
cp hookdex/build/hookdex-out/classes.dex "$OUT/classes.dex"

echo "== Zipping =="
( cd "$OUT" && zip -r9 "../$MODULE_ID.zip" . -x ".*" )

echo
echo "Done: dist/$MODULE_ID.zip"
echo "Flash it from KernelSU Manager (or Magisk Manager) -> Modules -> Install from storage."
