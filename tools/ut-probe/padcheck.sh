#!/bin/sh
# UTDeck stage 4.4: what Linux makes of each connected controller, before Wine sees it.
#
#   padcheck.sh            (on the phone, as phablet)
#
# For every HID device (USB or Bluetooth): its IDs and name, the kernel driver that took it, its
# input devices (/dev/input/event*) and its raw node (/dev/hidraw*) with owner, group and mode, and
# whether this user can open each one for reading. Then the kernel's recent HID messages (a driver
# that refused a controller says so there). Controllers only reach Wine through nodes this user can
# open: evdev for winebus' "input" path and SDL's fallback, hidraw for SDL's own controller drivers
# and winebus' hidraw path (the only one it takes PlayStation pads through).
can_read() { if [ -r "$1" ] && (exec 3<"$1") 2>/dev/null; then echo yes; else echo NO; fi; }
node() { stat -c "%n %U:%G %a" "$1" 2>/dev/null | awk '{printf "%s (%s, %s)", $1, $2, $3}'; }

found=
for d in /sys/bus/hid/devices/*; do
    [ -e "$d" ] || continue
    found=1
    id=$(basename "$d")                                  # BUS:VID:PID.N
    bus=${id%%:*}
    case "$bus" in 0003) bus=USB ;; 0005) bus=Bluetooth ;; 0006) bus=virtual ;; esac
    vidpid=$(echo "$id" | cut -d: -f2,3 | cut -d. -f1 | tr 'A-F' 'a-f')
    name=$(sed -n 's/^HID_NAME=//p' "$d/uevent")
    driver=$(basename "$(readlink "$d/driver" 2>/dev/null)" 2>/dev/null)
    echo "$name [$vidpid, $bus]"
    echo "  kernel driver: ${driver:-none}"
    for e in "$d"/input/input*/event*; do
        [ -e "$e" ] || continue
        n=/dev/input/$(basename "$e")
        echo "  input: $(node "$n"), readable: $(can_read "$n")"
    done
    for h in "$d"/hidraw/hidraw*; do
        [ -e "$h" ] || continue
        n=/dev/$(basename "$h")
        echo "  hidraw: $(node "$n"), readable: $(can_read "$n")"
    done
done
[ -n "$found" ] || echo "no HID devices (no controller connected?)"

echo
echo "This user: $(id -un), groups: $(id -Gn)"
echo
echo "Kernel HID messages (last 20):"
dmesg 2>/dev/null | grep -iE "hid|input:|xpad|sony|playstation|nintendo|microsoft" | grep -v PMIC | tail -20 | sed 's/^/  /' \
    || echo "  (dmesg not readable)"
