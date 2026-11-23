#!/bin/sh
# UTDeck stage 4.4: test a controller on the phone's USB port, where adb cannot stay connected.
#
#   systemd-run --user --unit=utdeck-usbpad --collect ~/utdeck-probe/usbpad-capture.sh [MINUTES]
#
# Started over adb, then adb is unplugged and the controller plugged in (a USB OTG cable). Waits up to
# MINUTES (default 15) for a USB device other than the phone's own, and each time one comes, writes into
# ~/utdeck-probe/usbpad-<time>.log:
#   - every USB device with its IDs, name, and each interface's class and bound driver;
#   - 12 s later (a driver that gives up, as hid-sony does with a DualShock 4 clone, takes about 5 s):
#     padcheck.sh (HID devices, drivers, evdev and hidraw nodes and who may open them) and the
#     kernel's messages since the device came;
#   - if a game controller's evdev node appeared: gptest.sh 60 (Wine: XInput, DirectInput, HID, and
#     winebus' decisions) while the user presses buttons, about 30 s after plugging in.
# A controller that drops off and is plugged in again is a new round. When MINUTES are up it ends;
# adb back on, the log is collected. (Started from adb, systemd-run needs the session's
# XDG_RUNTIME_DIR and DBUS_SESSION_BUS_ADDRESS: adb's shell has neither.)
P=$HOME/utdeck-probe
MIN=${1:-15}
SWITCH_LAYOUT='a:b0,b:b1,x:b3,y:b2,back:b9,start:b10,guide:b11,misc1:b4,leftshoulder:b5,rightshoulder:b6,lefttrigger:b7,righttrigger:b8,leftstick:b12,rightstick:b13,dpup:b14,dpdown:b15,dpleft:b16,dpright:b17,leftx:a0,lefty:a1,rightx:a2,righty:a3,'

LOG=$P/usbpad-$(date +%H%M%S).log
exec > "$LOG" 2>&1
echo "usbpad-capture: started $(date +%T), waiting up to $MIN min for a USB device"

usb_devices() {
    for d in /sys/bus/usb/devices/*; do
        [ -f "$d/idVendor" ] || continue
        case "$(basename "$d")" in usb*) continue ;; esac      # the root hubs
        echo "$(basename "$d") $(cat "$d/idVendor"):$(cat "$d/idProduct") \"$(cat "$d/product" 2>/dev/null)\""
        for i in "$d"/"$(basename "$d")":*; do
            [ -f "$i/bInterfaceClass" ] || continue
            drv=$(basename "$(readlink "$i/driver" 2>/dev/null)" 2>/dev/null)
            echo "  interface $(basename "$i"): class $(cat "$i/bInterfaceClass"), driver ${drv:-none}"
        done
    done
}

end=$(( $(cut -d. -f1 /proc/uptime) + MIN * 60 ))
round=0
# Every time a USB device comes (a controller can drop off and be plugged in again), until MIN is up.
while [ "$(cut -d. -f1 /proc/uptime)" -lt $end ]; do
    if [ -z "$(usb_devices)" ]; then sleep 2; continue; fi
    round=$((round + 1))
    echo
    echo "=== round $round: USB device seen at $(date +%T)"
    usb_devices
    sleep 12
    echo
    echo "=== round $round, 12 s later"
    usb_devices
    echo
    "$P/padcheck.sh"
    echo
    echo "=== the kernel's recent USB and controller messages"
    dmesg | tail -400 | grep -iE "usb|hid|input|xpad|sony|playstation|nintendo|microsoft" \
        | grep -vE "PMIC|usbpd|SC-USBPD|psy|batt_chg|Get usb type|usb online" | tail -40
    if python3 "$P/evtest.py" 0.1 2>/dev/null | grep -q '^/dev/input'; then
        echo
        echo "=== round $round: the kernel's view (evtest.py) and SDL's (sdlmap.py)"
        python3 "$P/evtest.py" 0.1 | sed -E 's/ {3,}//' | head -20
        python3 "$P/sdlmap.py"
        # A Switch pad on stone's hid-nintendo: SDL's own layout for it is off by one from Capture on
        # (stage 4.4); the corrected layout, under whatever GUID this bus and version give it.
        if python3 "$P/evtest.py" 0.1 | head -1 | grep -q "057e:2009"; then
            guid=$(python3 "$P/sdlmap.py" --guid | head -1)
            export SDL_GAMECONTROLLERCONFIG="$guid,Nintendo Switch Pro Controller,$SWITCH_LAYOUT"
            echo "corrected Switch layout for GUID $guid"
        fi
        echo
        echo "=== round $round: a game controller: gptest.sh 60 at $(date +%T)"
        (cd "$P/wine" && ./gptest.sh 60)
        unset SDL_GAMECONTROLLERCONFIG
    else
        echo
        echo "round $round: no game controller evdev node: no Wine test"
    fi
    # Wait for the device to go before looking for the next one.
    while [ -n "$(usb_devices)" ] && [ "$(cut -d. -f1 /proc/uptime)" -lt $end ]; do sleep 2; done
    echo "round $round: USB device gone at $(date +%T)"
done
echo "usbpad-capture: done $(date +%T), $round round(s)"
