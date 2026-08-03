#!/bin/bash
set -e

echo "[*] Preparing module structure..."
mkdir -p flashable_module/zygisk/arm64-v8a
mkdir -p flashable_module/zygisk/armeabi-v7a

echo "[*] Copying native binaries..."
if [ -f "native/libs/arm64-v8a/libdisable_hdr.so" ]; then
    cp native/libs/arm64-v8a/libdisable_hdr.so flashable_module/zygisk/arm64-v8a.so
fi

if [ -f "native/libs/armeabi-v7a/libdisable_hdr.so" ]; then
    cp native/libs/armeabi-v7a/libdisable_hdr.so flashable_module/zygisk/armeabi-v7a.so
fi

echo "[*] Packaging flashable zip..."
cd flashable_module
zip -r ../disable-hdr-zygisk.zip . -x "*.DS_Store"
cd ..

echo "[+] Build complete: disable-hdr-zygisk.zip"
