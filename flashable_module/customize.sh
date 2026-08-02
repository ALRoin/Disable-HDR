#!/system/bin/sh

ui_print "- Installing HDR Block Zygisk module"

set_perm "$MODPATH/classes.dex" 0 0 0644

ui_print "- Done. Reboot to activate."
