#!/system/bin/sh
# Runs once at install time (Magisk/KernelSU Manager calls this automatically).

ui_print "- Installing HDR Block Zygisk module"

# classes.dex is read by the module's companion process (running as root)
# and streamed into each target app process. World-readable is fine since
# only our own root-privileged companion ever opens it, but keep it tight.
set_perm "$MODPATH/classes.dex" 0 0 0644

ui_print "- Done. Reboot to activate."
