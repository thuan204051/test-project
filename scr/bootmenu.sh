#!/bin/sh
# ============================================================
# scripts/bootmenu.sh — CoolBooter boot menu
#
# Install to secondary OS as a startup hook so the user can
# choose to return to primary OS by holding Volume Down at boot.
#
# Install location (on secondary OS):
#   /etc/rc.d/bootmenu (if using BSD rc.d) OR
#   /Library/LaunchDaemons/com.coolbooter.bootmenu.plist
# ============================================================

SKIP_FLAG="/var/mobile/Media/CoolBooter/.boot_primary"
LOG="/var/mobile/Media/CoolBooter/bootmenu.log"
TIMEOUT=3  # seconds to wait for button input

# Detect Volume Down button state via IOKit
# On iOS, hardware button state can be read from:
#   /sys/class/input/event*/  (not available on Darwin)
# Alternative: check for physical button via IOKit
#   requires a compiled tool — shell script approach: check GSEvent or
#   fall back to a flag file approach

log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG"
    echo "$*"
}

log "[bootmenu] Secondary OS starting..."

# ── Check for "return to primary" flag ──────────────────────────
# The flag is created by coolbootercli -b (boot primary) command
# or by the user running: touch /var/mobile/Media/CoolBooter/.boot_primary
if [ -f "$SKIP_FLAG" ]; then
    log "[bootmenu] Skip flag detected — staying in secondary OS (flag removed)"
    # This flag means "boot PRIMARY on next restart FROM secondary"
    # We're already in secondary, so just remove it for next time
    rm -f "$SKIP_FLAG"
fi

log "[bootmenu] Running in secondary iOS"
log "[bootmenu] To return to primary: run 'coolbootercli --boot-primary'"

# The actual return-to-primary logic:
# From secondary OS, user can:
#   1. Run `coolbootercli --boot-primary` which creates the skip flag
#      and reboots — primary OS untether sees the flag and skips kloader
#   2. Hold power + home to hard reset — both OSes lose jailbreak
#      (semi-untethered jailbreaks like h3lix)
exit 0
