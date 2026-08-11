#!/system/bin/sh
# Runs once per boot, very early - KernelSU (and Magisk) both auto-detect
# and run a script with this exact filename if it exists in the module
# directory; nothing else needs to reference it.
#
# What this does, and why it's a *complement* to the native hooks, not a
# replacement: ro.surface_flinger.has_HDR_display is a real, documented
# AOSP system property - SurfaceFlinger's own sysprop schema at
# frameworks/native/services/surfaceflinger/sysprop/SurfaceFlingerProperties.sysprop
# defines it as "indicates that the device has an High Dynamic Range
# display", read-only, normally set once at build time via device.mk
# (confirmed directly from that file's current source:
# https://chromium.googlesource.com/aosp/platform/frameworks/native/+/refs/heads/master/services/surfaceflinger/sysprop/SurfaceFlingerProperties.sysprop -
# see README.md's "Is there a fundamentally different way to do this?" for
# the full trail). If SurfaceFlinger
# believes this is false, every app's Display.getHdrCapabilities() calls -
# and anything else that ultimately asks SurfaceFlinger/DisplayManager
# rather than the codec directly - should see "no HDR" without any
# per-app hooking at all.
#
# Two honest caveats, not glossed over:
#
# 1. TIMING IS UNVERIFIED. This is a `ro.*` (read-only) property.
#    `resetprop` (the standard Magisk/root tool for overriding those)
#    still only works if it runs *before* SurfaceFlinger itself reads the
#    property at its own startup - and SurfaceFlinger is one of the
#    earliest native services in the whole boot sequence, started from
#    init.rc, plausibly before post-fs-data-stage scripts like this one
#    even run. This was NOT tested on a real device from this sandbox
#    (no hardware access here) - it may simply be too late to matter on
#    your specific crDroid build. Check for yourself:
#      adb shell getprop ro.surface_flinger.has_HDR_display
#    after boot. If it's still your device's original value, this isn't
#    taking effect in time, and this file is a no-op you can safely
#    delete.
#
# 2. EVEN IF IT WORKS, IT'S PARTIAL. This can only affect the
#    Display/SurfaceFlinger detection path. Apps that query
#    MediaCodecInfo#getCapabilitiesForType() directly (Google's own HDR
#    playback guidance recommends exactly this for non-ExoPlayer apps -
#    see README.md) read HDR profile support straight from the decoder
#    HAL, which has nothing to do with this property. That path is what
#    the native Zygisk hooks in this module exist for, and this script
#    changes nothing about needing them.
#
# Fails safe either way: if resetprop isn't on PATH, or the command
# fails, this just logs and moves on - it never blocks or delays boot,
# and never touches has_wide_color_display (leaving your display's
# wide-color reporting exactly as the ROM shipped it).

LOG_TAG="DisableHdrZygisk-postfsdata"

if command -v resetprop >/dev/null 2>&1; then
  if resetprop ro.surface_flinger.has_HDR_display false 2>/dev/null; then
    log -t "$LOG_TAG" "set ro.surface_flinger.has_HDR_display=false (timing vs. SurfaceFlinger's own read is unverified - see comment in this file)"
  else
    log -t "$LOG_TAG" "resetprop found but the call failed - leaving ro.surface_flinger.has_HDR_display untouched"
  fi
else
  log -t "$LOG_TAG" "resetprop not on PATH - skipping the SurfaceFlinger property override (native hooks are unaffected by this either way)"
fi
