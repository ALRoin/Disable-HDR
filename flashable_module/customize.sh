#!/system/bin/sh
# Runs once at install time - the KernelSU (or Magisk) Manager app calls
# this automatically right after extracting the zip into place.

ui_print "- Installing Disable HDR module"

# targets.txt controls which apps get hooked: one package name per line,
# '#' starts a comment. Empty/missing file = hook every app (global mode).
# Only create it if it doesn't already exist, so re-flashing an update
# doesn't wipe out a list you built with the WebUI.
if [ ! -f "$MODPATH/targets.txt" ]; then
  touch "$MODPATH/targets.txt"
fi
set_perm "$MODPATH/targets.txt" 0 0 0644

# classes.dex is read straight off disk by the native module while it's
# still running as root, during preAppSpecialize - i.e. before the process
# drops privileges to the target app's own UID. No companion process and no
# permissions beyond world-readable are needed for that.
set_perm "$MODPATH/classes.dex" 0 0 0644

ui_print "- Done. Force-stop or reopen target apps to activate (no reboot needed)."
