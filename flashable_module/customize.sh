#!/sbin/sh
SKIPUNZIP=1

ui_print "- Extracting module files..."
unzip -o "$ZIPFILE" 'module.prop' -d "$MODPATH"
unzip -o "$ZIPFILE" 'sepolicy.rule' -d "$MODPATH"
unzip -o "$ZIPFILE" 'zygisk/*' -d "$MODPATH"

# Architecture detection
if [ "$ARCH" = "arm64" ]; then
    ui_print "- Installing 64-bit Zygisk binary..."
    mkdir -p "$MODPATH/zygisk"
    mv "$MODPATH/zygisk/arm64-v8a.so" "$MODPATH/zygisk/arm64-v8a.so" 2>/dev/null
elif [ "$ARCH" = "arm" ]; then
    ui_print "- Installing 32-bit Zygisk binary..."
    mkdir -p "$MODPATH/zygisk"
    mv "$MODPATH/zygisk/armeabi-v7a.so" "$MODPATH/zygisk/armeabi-v7a.so" 2>/dev/null
fi

set_permissions() {
    set_perm_recursive "$MODPATH" 0 0 0755 0644
}
