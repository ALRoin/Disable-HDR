#!/system/bin/sh

ui_print "- Installing Disable HDR module"
if [ ! -f "$MODPATH/targets.txt" ]; then
  touch "$MODPATH/targets.txt"
fi
set_perm "$MODPATH/targets.txt" 0 0 0644

set_perm "$MODPATH/classes.dex" 0 0 0644

ui_print "- Done. Reboot to activate."
