#!/usr/bin/env bash
# Builds the native Zygisk .so (all 4 ABIs) + the hook classes.dex,
# then assembles them with flashable_module/ into hdr_block.zip.
#
# Requires: Android SDK + NDK installed, ANDROID_HOME/ANDROID_NDK_HOME set.
# Run from the project root: ./package.sh

set -e

echo "== Building native module (all ABIs) =="
./gradlew :native:externalNativeBuildRelease

echo "== Building hook dex (HdrHook.java + YAHFA) =="
./gradlew :hookdex:extractHookDex

OUT=dist/hdr_block
rm -rf "$OUT"
mkdir -p "$OUT/zygisk"

echo "== Copying module skeleton =="
cp -r flashable_module/. "$OUT/"

echo "== Copying native .so per ABI =="
NATIVE_LIBS_DIR="native/build/intermediates/ndkBuild/release/obj/local"
for abi in arm64-v8a armeabi-v7a x86 x86_64; do
  src="$NATIVE_LIBS_DIR/$abi/libhdr_block.so"
  if [ -f "$src" ]; then
    cp "$src" "$OUT/zygisk/$abi.so"
    echo "  copied $abi.so"
  else
    echo "  WARNING: missing build output for $abi ($src)"
  fi
done

echo "== Copying classes.dex =="
cp hookdex/build/hookdex-out/classes.dex "$OUT/classes.dex"

echo "== Zipping =="
( cd "$OUT" && zip -r9 ../hdr_block.zip . -x ".*" )

echo
echo "Done: dist/hdr_block.zip"
echo "Flash it in Magisk Manager / KernelSU Manager -> Modules -> Install from storage."
