# Lessons learned

Every problem met while building ARMDeck on the OnePlus 8, what caused it and how it was solved
(or why it is still open). Newest first within each section. Details live in the linked documents;
this page is the short version, so the same mistake is not made twice.

## Kernel and boot

- **Random resets during big downloads (solved, kernel patch 0003).** The phone reset with nothing
  in the kernel log once memory filled up. Cause: the device tree deleted a reserved memory region
  (`removed_mem` at 0x80b00000) and never added it back, so Linux wrote into memory owned by the
  secure world. `stress-ng --vm 4 --vm-bytes 8000M` reproduced it in 5 s without any disk activity.
  Lesson: an instant reset with an empty log points at memory the kernel must not touch; check the
  reserved-memory map against the vendor device tree first.
- **Kernel Oops when the USB port switches from device to host (fixed in r12, 2026-10-05).** Kernel
  r12 carries the upstream fix and two follow-ups (patches 0008-0010, 0008 adapted to 6.16's
  goto-based `ncm_bind()` from the 6.12-stable backport). Tested with the gadget active: PC cable,
  then into the X3: `usb0` moved to `/sys/devices/virtual/net/`, `ip link` answered, no Oops; and
  a boot inside the X3 with its charger connected went through cleanly. The gadget-off workaround
  is no longer needed. History: the
  USB network gadget (`usb0`, SSH over the cable) outlives its parent device when the port goes from
  device mode (PC cable, charger) to host mode (GameSir X3). The next program that lists network
  interfaces reads freed memory in `rtnl_fill_ifinfo`, and the Oops keeps the network lock: WiFi,
  podman, the Steam session and even SSH logins hang. Upstream fix in Linux 7.0-rc4 ("usb: gadget:
  Fix net_device lifecycle with device_move"). Workaround: `armdeck-usb-gadget-off` removes the
  gadget at boot, and `op8-tune` sets `panic_on_oops=1`, `panic=10` so an Oops reboots the phone by
  itself. Lesson: a half-alive phone (ping and SSH banner answer, logins hang) means a kernel lock
  is held; look for an Oops first.
- **The hardware reset combination.** Power + Volume Down did not reset the hung phone, although
  the PMIC registers said it should. Power + both volume buttons held about 30 s did. Lesson: test
  recovery paths for real before relying on them; a register read is not a test.
- **Boot slot became "unbootable" (solved).** Without `qbootctl -m` at every boot, the bootloader
  counts down the retries and marks slot b dead after about 7 boots. `install-qbootctl.sh`.
- **`fastboot boot` hangs the OnePlus bootloader; `fastboot flash` can stall at "Sending".** Use
  the menu's "Restart bootloader", replug the cable, check `getvar partition-size:boot_b` first.
- **60 Hz test kernel showed a garbled image (r9, reverted).** Mainline derives the DSI link speed
  from the mode clock, so the 60 Hz mode ran the link at 348 Mbps, far below the 652.8 Mbps the
  vendor always uses. Lesson: for command-mode DSI panels, keep the link at the vendor's rate and
  change refresh through the panel's own commands.
- **A bandwidth without its path breaks the whole GPU frequency table (2026-10-27, avoided).**
  The comparison image `r17a` (r17 without the GPU's memory votes) was made by deleting
  properties from the r17 device tree. Deleting only the GPU's `interconnects` would have left
  an `opp-peak-kBps` in every GPU frequency entry; the OPP core then rejects each entry
  ("Mismatch between opp-peak-kBps and paths", `_read_bw()` in `drivers/opp/of.c`) and the GPU
  would have had no frequencies. Both go together; `r17/mkimg.sh` also checks the DTB difference
  line by line against what is expected. Lesson: read how the kernel parses a property before
  removing half of a pair.

## Display and performance

- **Steam interface slower than it should be (improved).** The DSI link ran at 521 Mbps, so one
  frame barely fit in the 11.1 ms of 90 Hz and missed frames fell to 45 fps. Kernel r11 widens the
  back porch so the link runs at 651.78 Mbps (vendor: 652.8): Quick Access 80 -> 88 fps.
- **The performance overlay cost half a CPU core (solved, mangoapp patch 5).** MangoHud's mangoapp
  never cleared its "new frame" flag and redrew at 90 Hz forever. After the fix it redraws only on
  new frame times, plus a 500 ms refresh for the sensors. Home scrolling 53 -> 60 fps.
- **The overlay vanished after a session restart (solved).** The redraw fix also drew before
  gamescope had sent the screen size, resized the window to 0x0 and hit an X error. Now it draws
  nothing until the size is known. Lesson: test a change through the full start-up path, not only
  by swapping the binary into a running session.
- **Steam's Frame Limit did nothing (solved), then halved the frame rate (open).** The limit is
  applied by gamescope's Vulkan WSI layer inside the game, which was missing from the container
  (`dnf install gamescope`, same version 3.16.29). With it, DXVK's frame pacing on this device
  dropped a 30 fps cap to 15-18 fps; turning that pacing off (`GAMESCOPE_WSI_FRAME_LIMITER_AWARE=0`)
  gave 30 fps but noticeable input delay, because frames queue up. MangoHud's in-game limiter
  (sleep-based, like RTSS: `MANGOHUD=1 MANGOHUD_CONFIG=no_display,fps_limit=30,fps_limit_method=late`,
  with Steam's limit off and `dnf install mangohud` in the container) gave a stable 30 fps with no
  delay the maintainer could feel (Tomb Raider, 2026-10-04). Next: drive it from Steam's slider.
- **Steam's slider driving MangoHud's limiter (solved, 2026-10-05).** gamescope patch 9003 hands the
  requested limit to a file, `op8-fpslimit` copies it into Steam's MangoHud file, and every game
  runs the MangoHud layer hidden with `read_cfg`. The first test showed no limit at all, although
  the chain up to the MangoHud file worked (the log shows 45, 30, 18, 0 following the slider).
  Cause: Proton ARM64 runs games in Steam's runtime container (pressure-vessel,
  `SteamLinuxRuntime_4-arm64`), which shares `/tmp` and the home directory but only the bus,
  PipeWire and Pulse sockets of `/run/user/10000`, so the game never saw `mangohud.conf`. The
  inline test had worked because `fps_limit` was in the environment, not in the file. Found by
  running a command inside the runtime (`SteamLinuxRuntime_4-arm64/run -- ls /run/user/10000`),
  after a headless test (`gamescope --backend headless` + `vkcube --c 300` in the container,
  timed) had shown that MangoHud, the file and the live reload all work. Fix: the file moved to
  `/tmp/armdeck-10000/`. Also, 9003 had stopped gamescope's pacing but still reported the limiter
  as engaged, so the gamescope WSI layer switched games to FIFO and DXVK recreated its swapchain
  at every slider move; the patch now reports it off. Result: Tomb Raider follows the slider
  (30, 18, 45, off) live, with no input delay the maintainer could feel. Lessons: a file shared
  "with the container" is not necessarily shared with the game's container, check from inside
  the runtime; and test a mechanism in isolation before blaming it.
- **"-r 60" held everything at 60 fps while the panel ran at 90 Hz.** In this DRM setup the nested
  refresh option only paces the apps. Removed from `steam-gamescope.sh`.
- **Black bars in every game (solved, gamescope patch 9001).** Steam does not treat the panel as an
  internal screen and asks Xwayland for 16:9 modes; the patch keeps the requested height and gives
  every mode the panel's 20:9 aspect (1280x720 -> 1600x720).

- **Native x86 Linux games closed at once (solved, 2026-10-05).** Steam's FEX tool needs an x86
  system with x86 Mesa at `/usr/share/guestos/fex-mesa`; FEX's own Arch Linux rootfs provides
  it (`install-fex-rootfs.sh`). Half-Life native then ran but slowly with idle CPU and GPU; that
  turned out to be the native Half-Life port itself (same reports on x86 PCs), so it stays on
  Proton. Lesson: before tuning the emulator, check whether the game behaves the same on a
  normal PC (ProtonDB).

## Steam session

- **Remote Play: two causes behind one crash (2026-10-10, solved 2026-10-11, TODO 11).** First, the firewall:
  with an input policy of drop, the PC's discovery replies never arrived and every stream went
  through Valve's relay; the logs said so ("connected via indirect connection") for a week
  before anyone read that line. Second, the crash itself: the coredump showed a run of 16-byte
  entries with a growing counter written across the decoder's own fields, which pointed straight
  at a fixed 16-entry table filled with 18 entries (Venus' minimum). Lessons: read the
  connection log lines, not only the error; and in a crash inside closed code, dump the memory
  around the bad pointer, since a pattern there names the writer faster than disassembly.
  Behind the crash was a third cause, a stall: Venus waits for a source-change handshake the
  client never does; strace showed it in one look (15 752 `QBUF`s, no `DQBUF`), and the
  driver source told exactly which calls it waits for. A preload that does those calls for
  the client fixed it without touching Steam's binary. Lesson: when closed code misuses a
  kernel API, read the driver's state machine; the fix can sit between the two.
  Moving that fix into the driver (patches 0016-0017, 2026-10-11) taught two more things.
  First, read the API's own spec before inventing a rule: V4L2's decoder spec already lets a
  client resume with buffers that fit (Capture Setup, step 6), Venus just never implemented
  it, and Qualcomm's own driver for this phone does the same on a "sufficient" event; spec
  plus vendor driver is the justification, not "it worked in the preload". Second, a
  userspace fix can block the client inside an ioctl and the kernel cannot: the preload held
  the client for 53 ms in its first `QBUF`, while in the driver the client's own 36 ms timer
  keeps running. A driver-side version has to survive the client restarting its queues in
  the middle (the decision is now kept across a `STREAMOFF`/`STREAMON`).
- **Steam interface never appeared after a reboot (solved).** The phone has no usable real-time
  clock, so Steam started with the date at 1970, its TLS connections failed and its web helper
  stalled. `steam-gs.service` now waits for the first time sync (at most 90 s).
- **"Switch to Desktop" froze the interface (solved).** Steam waits for `steamos-session-select` to
  end the session; the `op8-session-select` shim makes Steam restart in Game Mode.
- **Steam loses audio after a PipeWire restart.** Restart the Steam session too.
- **Launch Options are painful to type on a controller.** `op8-launch-options.py` edits them in
  `localconfig.vdf` while Steam is stopped (Steam rewrites that file on exit).
- **Every reboot took 93 s (solved, 2026-10-06).** The helper that stops Decky's plugins
  (`op8-decky`) ran in Steam's process group, so at shutdown it was killed together with Steam
  before it could do its job; the plugins kept gamescope's reaper waiting until systemd's 90 s
  stop timeout. Its own session (`setsid`) and a signal trap fixed it (4 s now). The earlier
  check had only covered a Decky restart, never the "Steam ended" path, whose log line had in
  fact never appeared. Lessons: a cleanup helper must not share the process group of what it
  cleans up after; test every exit path (session restart, shutdown), and look for the log
  line each path should write.
- **A Steam beta update stopped three Decky plugins from loading (2026-10-30).** ARMDeck and
  SteamGridDB failed with `Cannot read properties of undefined (reading 'Title')`, Game Theme
  Music with another error; their Python side loaded fine, so nothing showed in Decky's logs.
  Steam's console (read through its local debugging port, 127.0.0.1:8080) had the reason:
  `@decky/ui` finds Steam's Quick Access style module by a list of class names, the new Steam
  build dropped one of them (`BatteryDetailsLabels`), and `staticClasses` became undefined.
  Plugins get `@decky/ui` from Decky itself at run time (the `DFL` global), so updating it in a
  plugin's own `package.json` changes only the types; the fix in `@decky/ui` (2026-10-17) reaches
  Steam only with a newer Decky (upstream v3.3.0). ARMDeck now looks the module up itself when
  `staticClasses` is missing. Upstream Decky had shipped official ARM64 builds 12 days earlier,
  unseen because Decky was not in `docs/sources.md`; it is now, with DroidDeck's fork and
  `@decky/ui`. Lessons: read a plugin's error in Steam's console, not only in Decky's logs; a
  plugin must not let one optional lookup stop it from loading; every project we run belongs in
  the sources list, not only the ones we patch.
- **The SteamGridDB plugin took 14 GB of memory and made Steam crawl (2026-10-30, open).** Right
  after Decky v3.3.1 started and SteamGridDB updated itself to v1.8.1 (Python 3.14 in Decky now),
  its Python process grew to 7.5 GB of RAM plus 6.7 GB of swap within 6 minutes, with 350 MB of
  RAM left. kswapd and swap kept the CPU busy (51 % system time, 87-93 °C, at the kernel's 90 °C
  throttling point) and the battery at 2.1-2.3 A; Steam's menus and Quick Access lagged badly,
  which first looked like the Steam interface being drawn by the CPU. It is not: Steam's GPU
  report and the GPU process' open `/dev/dri` show ANGLE on freedreno (FD650, Mesa 26.2.3) with
  GPU compositing and rasterization on. Stopping the SteamGridDB process brought 8.6 GB back at
  once. Lessons: after any Decky or plugin update, watch the memory of every plugin process for
  a few minutes; when the interface lags, look at memory and swap first (`top`, `/proc/meminfo`),
  then at the GPU.
- **Decky reloaded CSS Loader 12 times a second, without end (2026-10-30, solved).** With
  SteamGridDB stopped, the idle Steam menu still drew about 670 mA (380 mA on 2026-10-27, the X3
  controller attached adds some) and the little cores were 28-36 % busy. `op8-decky.log` grew by
  1.2 KB a second with "Plugin CSS Loader (v2.1.3) is already loaded and has requested to not be
  re-loaded". The file contents had not changed since its 15:05 update, but their ctime was the
  current second. Decky's `PluginWrapper` asks whether a plugin folder belongs to the effective
  user; `file_owner()` checks the host user first, and when Decky runs as that same user (ours
  does) the answer is never "effective user", so every look at a plugin ran `chown -R` and
  `chmod -R 755` on its folder. The hot reload watcher (watchdog) reports those metadata changes
  as modified files and queues the plugin again: a loop, started by the plugin update. Fix:
  `op8-decky` sets Decky's own switches `CHOWN_PLUGIN_PATH=0` and `LIVE_RELOAD=0`. Found on the
  way: `deploy.sh`'s `install.sh` wrote files in place (BusyBox `install` truncates and rewrites),
  which a running shell script such as `op8-decky` would then read from the middle; it now
  moves a new file into place. After a Steam session restart with the new `op8-decky`: no reload
  attempts in 30 s, and the settled idle menu at 423 mA over 40 s with the X3 attached, CPU 3 %
  busy, every core and the GPU at their lowest frequency. Lesson: when something keeps a loop
  going, look at what changes between two turns (here the ctime) rather than at the contents.
- **PPSSPP's pause menu could not be opened from the controller (2026-10-30).** Steam keeps
  the Guide button, the only controller button PPSSPP's SDL build treats as "back to the menu"
  (`NKCODE_BACK`). The default mapping also puts Pause on the left trigger, but neither trigger
  did anything: the SDL build passes SDL's own axis number on (`SDLJoystick.cpp`, the left
  trigger is axis 4), while the default `controls.ini` waits for axis 17 (`10-4034`, PPSSPP's
  `JOYSTICK_AXIS_LTRIGGER`). Way around: map Pause from PPSSPP's own window (Settings > Controls
  > Control mapping), which records whatever the trigger really sends. Fix in `armdeck-run`:
  before PPSSPP starts, once, Pause also gets L3 + R3 (as RetroArch's menu) and the left trigger
  as SDL reports it (`10-4008`). On the phone the same evening: both open the menu. Still to
  do: the same for the other emulators (TODO 38).
- **PPSSPP ran with four Vulkan drivers loaded (2026-10-30).** Its "anylinux" AppImage (sharun)
  brings Mesa 26.0.6; on aarch64 its hook asks sharun to add the system's drivers too, so the
  session's `VK_DRIVER_FILES` (the container's Turnip only) ended up behind the bundled Turnip,
  `lvp` with LLVM and `dzn`. The process maps showed all four, and two "Turnip" shader cache
  folders. Lesson: an AppImage's environment is set by its own launcher after ours, so check
  what the running process really loaded (`/proc/<pid>/maps`), not the variables we passed.
  Fixed in the Library: the unpacked copy loses the bundled driver list and `armdeck-run` sets
  `SHARUN_ALLOW_SYS_VKICD=0` (gaming-stack.md TODO 38). Checked on the phone the same evening:
  the running PPSSPP maps only the container's Turnip.

## Power, charging and controllers

- **GameSir X3 Pro pass-through charging (solved, DT `sink-wait-cap-time-ms = <620>`).** The X3
  asks for a power role swap only right after the phone is seated with its charger already
  connected. Its capabilities then arrive 337 ms after the swap, but Linux waited only 310 ms (the
  USB PD minimum), sent a hard reset and came back as a USB device, so the controller vanished;
  the X3 refuses a data role swap in that state. Lesson: capture the USB PD state machine log
  (tcpm, `op8-usbpd-capture`) before guessing.
- **Charging when the charger goes into the X3 after the phone (not possible, 2026-10-05).** The X3
  asks for the power role swap only when the phone is seated; it ignores a swap requested by the
  phone (`echo sink > /sys/class/typec/port0/power_role`: the request is acknowledged, then no
  answer for 60 ms, harmless). A software "re-seat" (port_type sink then dual, which opens the CC
  lines for 100 ms, or repeated for 8 s) made the X3 swap and the phone charged for 2-3 s, then
  the X3 swapped back by itself, and a later swap ended in a hard reset with the controller gone
  until the phone was re-seated. GameSir's own FAQ says the same order problem exists on Android:
  charger into the X3 first, wait for the fan, phone last. Also from the manual: S + D-pad Right
  toggles pass-through charging (saved across restarts), S + D-pad Up/Down sets the cooler power,
  and "Extreme Cold" needs a 9 V / 3 A charger. Lesson: read the accessory's FAQ before
  engineering around it; the limit may be the accessory's.
- **The X3 stuck as a USB host, controller gone (solved by power-cycling the X3, 2026-10-05).**
  After the swap experiments above, every attach ended within 0.6 s: the X3's Request came with
  the data role bit set to DFP (header 0x1062 instead of 0x1042), both ends claimed to be host,
  and tcpm did an error recovery ("Data role mismatch"), forever. The X3 keeps its state while its
  charger powers it, so re-seating the phone did not help; phone and charger out for ~15 s, then
  charger, fan, phone, cleared it. Lesson: when an accessory has its own power, it can carry a bad
  state across re-seats; power-cycle it before suspecting the phone.
- **The battery ran flat during a long PC session (2026-10-06).** The phone stayed in the Steam
  interface while the work happened on the PC, and it shut down at 0% (the gauge's 0%, 3.56 V
  after shutdown, is still above the cell's own cut-off). **Corrected 2026-10-27:** it did not
  shut down. The sampler log shows it kept running at the gauge's 0% for 25 minutes below
  3.25 V and died when the battery cut out at 2.65 V; the 3.56 V was the cell recovering
  afterwards. Nothing on the system turned the phone off on a low battery (now `op8-lowbatt`, see
  below). On the PC's USB port it then charged
  at only ~0.66 A, about 3.3 W, little more than the idle Steam session uses (TODO 33); on a
  45 W charger (PD 5 V / 3 A, patch 0004) the battery got 0.76 A at 0%, the PMIC's default limit
  without a charger driver. Lessons: keep the phone on a wall charger during long sessions,
  stop `steam-gs` when it only needs to charge, and keep checks on a low battery read-only and
  light.
  Afterwards (2026-10-06, 21:32) the gauge reported 1% while charging, at 3.85 V and with
  `charge_now` 526 mAh of `charge_full` 3093 mAh (about 17%): after the full discharge its own
  percentage lost track. The battery itself was fine; the number is only to be trusted again
  after a full charge, and until then the phone stays on the charger (a low percentage on
  battery can trigger the system's low-battery power-off).
  **It came back, worse (2026-10-27, open).** The gauge said 1-3% while charging at 4.1-4.2 V
  (about 4.1 V resting, so most of the charge is there), with `charge_now` 2485 mAh above
  `charge_full` 1234 mAh. The sampler log (`/var/log/op8/sample-*.csv`) shows how it got there:
  in a game the battery gave 3.6-3.8 A and sagged to 3.32 V, below the 3.4 V terminate voltage
  the driver writes from the device tree, and within 20 s the gauge jumped from 48% to 0%; since
  then it counts up from that 0. Its learned full capacity fell from 3056 mAh (2026-10-01) to
  1234 mAh in four weeks, too fast for real wear, so the gauge's learning is suspect as well. The
  sag itself is real: about 0.18 ohm (435 mV over 2.44 A), a worn cell. OnePlus' driver for this
  board (LineageOS `drivers/power/oplus/gauge_ic/oplus_bq27541.c`, DT `vendor/19821/kona-mtp.dtsi`
  with `qcom,modify-soc-smooth`) rewrites three gauge settings at every boot and at shutdown
  (Op Config B, CC dead band, DOD at EOC) and resets the gauge at shutdown when it is at 0% below
  3.3 V; mainline does none of this. The OnePlus 8 Pro (19811) has a different cell and its own
  DT: never take values from it. Lessons: the sampler log is the first place to look for battery
  questions; and a percentage that jumps to 0 under load is the gauge reacting to voltage sag,
  not a battery that is empty.
  Read straight from the gauge the same day (`i2cget -f -y 16 0x55 <reg> w`, standard commands
  only, all readable while sealed per TI SLUUAS7B), at 4.18 V while charging: StateOfCharge 15%
  (what the driver and Steam show), StateOfChargeUnfiltered 62%, NominalAvailable / FullAvailable
  2650 / 3155 mAh (84%, no load), FullChargeCapacityUnfiltered 1317 mAh, StateOfHealth 69%,
  Flags 0x0188 (CHG, OCVTAKEN, BAT_DET; no ITPOR, so the configuration is intact). So the cell
  holds about 3.1 Ah; the gauge expects to deliver only 1.3 Ah of it before 3.4 V at the load it
  assumes; and the shown 15% is the smoothing filter (Op Config B [SMOOTHEN], on by TI default
  and on OnePlus), which after the forced 0% "is not allowed to jump" and only climbs slowly back
  to the real value, converging at full charge. Three things to fix: the terminate voltage
  (3.4 V from the Xo666 DT, while OnePlus powers off at 3.25 V, `vbatt_power_off`), what the
  driver reports, and a clean power-off at low voltage (`sample-0060` ends in NUL bytes: the
  phone lost power without a shutdown).
  **Deep discharges (2026-10-27, fixed with `op8-lowbatt`).** The minimum voltage of every
  sampler log since 2026-10-03: two runs went to 2.65 V (2026-10-06 and 2026-10-12, idle in
  Steam at about 0.38 A, 20-25 minutes below 3.25 V and 7-11 minutes below 3.0 V), both ending in
  a power cut. Short dips under load are normal (2.95 V for one sample at 2.4 A, 3.09 V and
  3.17 V for two samples) and come back within seconds. `userspace/system/op8-lowbatt` now powers
  off cleanly when the battery stays below 3.25 V for 15 s while discharging: OnePlus' rule for
  this phone (`oplus_charger.c`: the gauge's 0% is held at 1% while the battery is above
  `vbatt_power_off`, 3250 mV in `vendor/19821/kona-mtp.dtsi`; below it the level runs down 1%
  every 15 s to the power-off). Lesson: a protection the phone had on Android is not there on
  mainline unless someone adds it; list what the vendor's charger code does, not only its
  drivers.
  **"Stuck at 96%" on the charger (2026-10-27, r16, not a fault).** The PMIC had ended the
  charge by itself: charger state TERMINATE (`0x1006 = 0x45`, OnePlus `smb5-reg.h`), float
  4.37 V (`0x1070 = 0x4d`), recharge only below 4.20 V (VBAT-based, `0x1051 = 0x2c`; threshold
  `0x107e/0x107f = 0x544a`, x 194.637 uV = 4200 mV). The current went from about 0.6 A straight
  to 0 at 4.35 V, with no taper, so the gauge never saw its own end-of-charge condition and
  never reached 100%; resting at 4.26 V the cell holds about 90%. These are the bootloader's
  settings (same values as on 2026-10-01; no charger driver touches them). OnePlus instead
  charges to 4.435 V, ends at 220 mA (`qcom,iterm_ma`) and recharges 100 mV below float. Not
  changed: PMIC charging values stay hands-off (hardware-safety.md 4.1), and a lower full point
  is easier on a worn cell. What 0x1007 = 0x28 means on the PM8150B is not documented in the
  vendor header and was not guessed.
- **PD chargers could ask for 9 V (solved, patch 0004).** The device tree allowed sink PDOs up to
  12 V, while this phone never used PD above 5 V on Android. Now 5 V only.
- **Cameras and modem drew power while unused (solved, patch 0005).** Every camera supply had to be
  turned off, including L3F/L7F (a first version left the front sensor half powered); checked
  against the LineageOS vendor device tree.
- **Bluetooth found no devices (solved).** The QCA6390 reports a default address, so the controller
  stayed "unconfigured". `bootmac` sets an address at boot.
- **X3 Capture button sent Power (solved).** In its blue (HID) mode it sends Volume Down + Power,
  Android's screenshot chord. White mode (DualSense) makes it a normal button for Steam Input.
- **Leftover debugging costs (solved).** `op8-top`, a `sync` every 2 s in the sampler, journald
  syncing every 2 s, SDL verbose logging, and real-time tasks forcing the CPU to its top clock
  (`sched_util_clamp_min_rt_default` now 0, no audio crackles).
- **A sleep fix ruled out by its title (2026-10-27, patch 0019, to measure).** The source check
  of 2026-10-27 found gennro's fix (SteamOS-ARM-Handhelds, Armada `0568`): the CPUs' memory
  (DDR) votes stayed in the "sleep set", the votes the power controller applies while the
  processors are powered down, so the RAM never reached self-refresh. It was parked as "only for
  real suspend (s2idle)", which ARMDeck does not use. Wrong: in our `sm8250.dtsi` the controller
  that applies those votes (`apps_rsc`) sits in the CPU cluster's power domain (`cluster_pd`,
  idle state `cluster_sleep_0`, about 10 ms minimum), so the sleep set is used every time all
  eight cores idle long enough: in `op8-standby` and in quiet moments with the screen on. Armada
  reports about 25% less sleep power on SM8250. Lesson: check where a fix's mechanism triggers in
  our own device tree before ruling it out by its title. Baseline before the test, kernel r16,
  2026-10-27 evening (Steam menu idle, USB unplugged, WiFi on, battery current from the sampler,
  steady minutes only): screen on about 380 mA (about 1.55 W at 4.06 V), screen off about
  255 mA (about 1.05 W at 4.07-4.11 V, 16 minutes before plus 6 minutes in the test). Test image
  `r17a` (0019-0021 without 0022), 2026-10-29, measured the same way: screen on about 395 mA at
  full brightness and about 300 mA once Steam dims the idle screen (both as on r16), screen off
  about 256 mA, so no measurable gain. Why: every core still enters idle about 260 times a
  second (`cpuidle` usage counters over 30 s, read right after the test with the cable back in),
  so a typical idle period is about 3 ms, shorter than the 10 ms the cluster needs before it
  powers down and the sleep set is used. Over the same 30 s the CPUs got about 500 "function
  call" interrupts a second (one CPU asking the others to run something; almost all of them, the
  trace showed, are wake-ups of a task on another core) and about 830 timer interrupts. A 10 s
  kernel trace with the screen off and the cable out (`wake-trace.sh`: trace events for
  interrupts, timers and woken tasks) found about 690 task wake-ups a second, more than half of
  them PipeWire's audio threads running a 94 Hz cycle although nothing was playing. Lesson:
  before measuring a sleep-state fix, check that the system reaches that sleep state at all, and
  note the screen and cable state of every reading. 0019 stays (it is correct and costs nothing);
  the wake-ups are the open problem.

- **Standby kept the audio path and the panel running (2026-10-29, solved).** "Screen off" in the tests above
  is `op8-standby`: the power button makes Steam ask for sleep, the script sets the brightness
  to 0 and freezes Steam, gamescope and the game, while PipeWire keeps running. A logger
  (`audio-wake.sh`, every 2 s: open ALSA streams, PipeWire wake-ups, interrupts, battery
  current) showed an ALSA playback stream opening at the moment of the press and staying open
  for the whole 207 s of standby, PipeWire's audio thread running at 94 Hz: Steam starts a
  sound as it asks for sleep and is frozen in the middle of it, so its stream never stops and
  the audio DSP, codec and amplifiers keep working. After the wake-up Steam finished the sound
  and the device closed by itself within a minute. The logger itself, started with
  `systemd-run --user`, sat in the frozen `app.slice` and wrote nothing during standby (one
  line covering the 210 s at the end); a measurement during standby must run as a system
  service. Suspending the device from outside (`pactl suspend-sink`) would not hold while a
  stream is linked; PipeWire's own `pulse.idle.timeout` pauses a client's stream after N
  seconds without data and plays it again when data comes (`audio/50-op8-pulse-idle.conf`, 5 s,
  the value PipeWire's own configuration uses for speech-dispatcher). The second half of the
  loss was the panel: `op8-standby` only set the brightness to 0, so the display controller
  kept reading the picture from memory (about 3.8 GB/s of DDR votes with the GPU asleep) and the
  panel stayed powered; the sleep-stage test of 2026-10-03 (`op8-power-test`) had already
  measured 275 -> 179 mA for "panel off through gamescope", and that step had never been
  added to `op8-standby`. Now it switches the panel off with `gamescopectl
  drm_sleep_internal_screen 1` before freezing, and back on after the CPU limits and the apps.
  Result over 25 minutes of standby on r17: 160 mA (0.60 W at 3.75 V) against 256-266 mA
  (1.01-1.05 W) before, 0 running ALSA streams 20 s after freezing, the panel back at wake-up.
  With about 2500 mAh usable that is about 15 hours of standby, against 9-10 before. One
  slip: the brightness written right after `drm_sleep_internal_screen 0` failed with "Invalid
  argument" (gamescope switches the panel on asynchronously; Steam set its own brightness a
  moment later); the script now waits for the panel to report On first (checked the same day:
  "leaving standby: panel On", no error). Lesson: when an old
  measurement already shows a big saving, check that it went into the product.

- **Real suspend works, but the SoC never reaches its deep sleep (2026-10-29, open).**
  `op8-s2idle-test 300` on r17 (panel off, apps frozen, `echo freeze > /sys/power/state`, RTC
  wake-up): suspended 302 s, woke by itself, average 107 mA from the battery charge (9 mAh, so
  about +-11%), against 160 mA for the fixed `op8-standby`. The power button is a wake-up source
  (`pm8941_pwrkey`, `power/wakeup` enabled). But `qcom_stats` `aosd`, `cxsd` and `ddr` stayed at
  0: no AOSS sleep, no CX rail collapse, no DDR self-refresh. The test now also saves what is
  still on just before and after the suspend (`.before`/`.after`). Three holders found in the
  power domain summary:
  - `cx` at level 48 (low SVS) held by `a90000.serial`, the debug UART used as the kernel
    console (runtime PM "unsupported"); nobody uses it on a closed phone;
  - `mmcx` at level 64 held by `ad00000.clock-controller`, the camera clock controller, also
    without runtime PM, although the cameras are off (patch 0005);
  - `pcie_0_gdsc` on, `1c00000.pcie` and the WiFi chip (`17cb:1101`, ath11k) active in D0: with a
    device on the link, `pcie-qcom` keeps the controller's resources on through the suspend.
  Side notes: the touch controller (`s6sy761`) fails its resume callback with -19 (it is powered
  with the panel, which is still off at that point) and works anyway afterwards; ath11k logs
  "failed to process regulatory info -22" at resume and WiFi reconnects. "psci: [Firmware Bug]:
  failed to set PC mode: -3" at boot is harmless: the next line is "Initialized CPU PM domain
  topology using OSI mode".
  Reading the 6.16 drivers took two of the three suspects off the list: the geni UART's
  `uart_suspend_port()` path powers the port off (OPP rate 0, interconnect off) in system
  suspend, and genpd votes a power domain off once all its devices are suspended, so the
  console's and the camera clock controller's votes only cost power while awake. The PCIe
  controller has no interconnect votes on SM8250 (no `interconnects` in its node), but
  `qcom_pcie_suspend_noirq()` keeps its clocks, PHY and GDSC on whenever the link is up.
  Experiment (`op8-s2idle-test 300 nowifi`: WiFi device removed from the bus and powered off
  through its pwrctrl before the suspend): 83 mA instead of 107, still 0 in all three
  counters, so PCIe is one holder among others. Costs of that route: ath11k's firmware crashed
  into RDDM during the removal, and 6.16 does not train the link again at resume (WiFi back
  only after a reboot); upstream solved it properly in May 2026 ("PCI: qcom: Add D3cold
  support", 2cc0e7454c78, part of a series). For scale: Armada's `0568` notes that an SM8250
  RP5 slept at about 1.7% an hour before the same CPU-vote fix (about 85 mA of its 5000 mAh)
  and about 25% less after it, so on this SoC the deep states save tens of mA, not hundreds.
  Their `armada_sleep_debug.py` traces clock, regulator and interconnect events across a
  suspend to name what is still on at the end; that is the next tool if we go further.

- **Deep sleep in standby held back by its own review (2026-10-29, open).** A version of
  `op8-standby` that goes into s2idle on battery (RTC check every 30 min, light standby with a
  cable or a low battery) was reviewed by three independent agents before any install, at the
  maintainer's request. Not deployed; what they found, checked against the 6.16 sources:
  - charging: the pm8150b Type-C driver (`qcom_pmic_typec`) arms no wake-up and has no resume
    handler; the SPMI interrupt controller masks its interrupts in suspend and clears their
    latched state on unmask, so a cable plugged or pulled during s2idle is never seen by TCPM.
    The PMIC still charges at 5 V on its own, but the worst case is real: phone powering the X3
    (OTG, `online` = 0, so deep sleep allowed), phone taken out during sleep, the boost keeps
    5 V on the empty port, and a USB-C charger plugged in later never switches on. `online` is
    also 0 whenever the phone is the power source; `/sys/class/typec/port0-partner` is the
    better "something is plugged in" signal;
  - battery: a hang in a driver's resume (ath11k is the likely one) has no recovery: the
    hung-task detector is off during suspend, systemd's runtime watchdog is off
    (`RuntimeWatchdogUSec=0`, `/dev/watchdog0` exists), so the phone would sit at about 100 mA
    until the cell cuts out, the October failure again. 3500 mV is a thin margin on a worn cell
    whose charge between 3.5 and 3.25 V was never measured, and the voltage right after a
    charger is pulled reads high;
  - the script: a SIGTERM ran `restore` and then let the loop go on (it could re-suspend with
    nobody watching the button), a dead or late button watcher meant a forced reset,
    `gamescopectl` had no timeout, and a killed run left `/run/op8-standby.active` behind, which
    switches `op8-thermal`'s caps off until reboot. Some of these also hold for the light
    standby on the phone now.
  Since then (same day): the light standby got the script fixes and its emergency exit was
  tested (`systemctl kill`, see Tools), and kernel r18 (patch 0024) makes the Type-C driver read
  the port again at every resume. Test: a charger plugged in during a 300 s s2idle did not wake
  the phone, the PMIC charged on its own at about 1.2 A (+101 mAh), and right after the RTC
  wake-up TCPM saw it (`online` 1, `port0-partner`, PD traffic). A cable pulled during the sleep:
  after the wake-up `online` 0, no partner, sink role, the VBUS boost regulator off. Still to do
  before deep sleep: the watchdog, wider battery thresholds, the X3 (phone as the source), a
  soak test.
  Lesson: a sleep mode that the kernel enters on its own must have a way back that does not
  depend on the code that may have hung, and every "cable present" test must cover the phone
  being the power source as well as the power sink.
- **A Steam download took the battery to 56.4 °C; the thermal guard could not stop it
  (2026-10-30, fixed and tested on the phone).** From the op8-sampler log (times in EET):
  - 00:57-01:25: a Steam download of about 80 GB at about 48 MB/s over WiFi, with the phone
    powered from USB. Load average 13-17, CPU pressure 26-35 %.
  - `op8-thermal` reached its top level (4) at 01:04, at 44.5 °C. That level caps only what a
    game heats: the big cores, the prime core and the GPU. The download ran on the little cores
    (left at 1.8 GHz), WiFi and UFS.
  - The battery kept rising for 20 minutes, from 44.5 °C to 56.4 °C at 01:24. The CPU was at
    90 °C, the PMIC at 80 °C, the WiFi chip at 80 °C.
  - When the download ended (01:25), the phone charged at 1.1 A with the battery at 56 °C. The
    PM8150B's JEITA compensation is off (`JEITA_EN_CFG` = 0x00, `pmic-jeita-20261002` readout), so
    only its hard hot limit would have stopped charging.

  The maintainer then cooled the phone in a freezer (the battery reached 12 °C in 10 minutes)
  and ran the Tomb Raider benchmark:
  - the cold, worn cell sagged to 3.21-3.29 V at 2.4-2.6 A, while it rested at 3.91 V before
    the load: about 0.29 Ω, against about 0.19 Ω at 25 °C the same evening (3.49 V at 3.0 A
    from 4.02 V);
  - the gauge reached its empty point (3.25 V, patch 0018) and dropped from 69 % to 0 % at once;
  - `op8-lowbatt` powered off cleanly after 16 s below 3.25 V.

  After the boot, at 3.86 V and 23 °C, the gauge still read 0-1 %. A full charge should correct
  it. The fix (same day):
  - `op8-thermal` caps the little cores from level 3, and a level 5 at 47 °C holds everything
    near its minimum (that slows a download by itself);
  - at 52 °C a pause in standby, once per episode, and at 58 °C for 30 s a clean power-off;
    OnePlus' own values for this phone: charging reduced from 45 °C, stopped from 50 °C, power-off
    only above 77 °C (hardware-safety.md 4.8);
  - charging cannot be stopped from Linux here: there is no charger driver, the PMIC charges on
    its own, and its registers stay untouched. The ARMDeck plugin asks in Steam to unplug the
    charger when the battery charges at 45 °C or more.

  Tested on the phone the next day with `system/op8-thermal-test` (fake temperatures from a
  file, the real battery at 26 °C): every level's limits read back from sysfs, cooling one level
  at a time, a jump straight to level 5, one real standby pause woken with the power button, the
  power-off counter running inside the standby and reset after 15 s, no second pause after
  waking, the pause armed again below 47 °C. 0 failures. A first copy of the test printed wrong
  wait times: its wait loop and its `hold` function shared one variable (`n`), since `sh`
  functions have no local variables by default; the waits themselves were right.

  Lessons: thermal protection must cap every heat source, not only the ones games use, and
  each level must be tested with a non-game load too. A freezer is not a cooler: cold raises a worn
  cell's sag under load by half, the phone gets condensation inside when it comes out, and
  charging a cell colder than about 10 °C at full current risks lithium plating (the reason for
  JEITA's cool zone).

## Audio

- **Speakers quiet and earpiece quieter.** The TFA9874 amps are protected by a filter chain with a
  ceiling and the earpiece side lowered 3 dB. Never test with a raw `aplay` on the host.
  2026-10-06: an average-power limiter (LSP Compressor Mono, RMS) went into the filter and the
  ceiling from -18 to -12 dB, with the worst-case average power unchanged. The maintainer still
  found it much quieter than Android; a capture of what reached the speakers showed why: the
  video playing came in at -28 to -35 dBFS RMS with peaks at -14 dBFS, far below the limiter,
  so the source itself was quiet. Lessons: measure the signal before judging a volume step by
  ear; test a new filter on a null sink (no speakers) with known tones first, and check that it
  links only there before playing anything.
- **Bluetooth headphones paired but never connected (solved, 2026-10-05).** bluetoothd
  logged "a2dp-sink profile connect failed: Protocol not available": the PipeWire Bluetooth
  plugin (`pipewire-spa-bluez`) was not installed, so nothing offered the audio profiles. After
  `apk add pipewire-spa-bluez` and a WirePlumber restart, Galaxy Buds3 Pro play over A2DP with
  AAC. The kernel has no RFCOMM (`CONFIG_BT_RFCOMM`), so the Hands-Free profile could never
  connect, yet the phone advertised it and the headphones tried it first, so they did not
  connect by themselves. `51-op8-bluetooth.conf` keeps only the audio roles (no Hands-Free; the
  microphone is not wanted): now they connect by themselves when taken out of the case. After a
  "Disconnect" from Steam they stop answering ("Host is down") until they go back in the case;
  the page from the phone gets no answer, so it is the headphones' own behaviour. The delay in
  games is noticeable (AAC, mostly the headphones' buffer). When they disconnect, the default
  output falls back to "Speakers (protected)" by priority (2000; headphones 1010, raw speaker
  100; checked 2026-10-05 with no sound playing), and WirePlumber remembers the headphones as
  the chosen output, so it switches back to them when they reconnect.
  Lesson: a Bluetooth device that pairs but does not connect usually means a missing profile
  provider; read bluetoothd's log first.
- **Know which boot a log line belongs to (2026-10-05).** The op8 boot reports list the kernel
  warnings of the *previous* boot. Counting them as the current boot's put every audio failure
  one boot off; read the section header before correlating.
- **A Vulkan layer in the home directory is also seen by the host (2026-10-05).** The Steam
  container shares `/home` with postmarketOS, so an implicit layer manifest in
  `~/.local/share/vulkan/implicit_layer.d/` is read by the host's Vulkan loader too (gamescope).
  lsfg-vk's manifest is always on; forced on the host, the musl loader crashed on its glibc
  library. Our build makes the layer opt-in (`enable_environment`), checked with
  `VK_LOADER_DEBUG=all` before any game used it.
- **The postmarketOS host has no curl (2026-10-05).** `install-decky.sh` stopped at its download
  when run on the host; BusyBox has `wget` only. Download scripts run in the container, or fall
  back to `wget`.
- **A live config file is not a free control (2026-10-06).** lsfg-vk rebuilds everything on
  each change of its file; a Decky slider that wrote on every step froze the game. Controls that
  write files a running program watches must wait until the user stops moving them.
- **A helper process that outlives Steam blacks out the screen (2026-10-05).** gamescope's reaper
  waits for every process of the session; two Decky plugins that ignored SIGTERM kept
  `steam-gs` in "deactivating" with a black screen. Helpers that start children run them in a
  process group of their own and stop the whole group, by force if needed.
- **Steam's own keyboard shortcuts are in its logs (2026-10-05).** `logs/*.txt` list "Registered
  global hotkey": Shift+Tab for the Steam menu, Shift+Ctrl+Tab for Quick Access. A virtual
  keyboard sending them is all a spare button needs.
- **Software made for SteamOS expects SteamOS's services (2026-10-05).** Decky restarts itself
  through `systemctl restart plugin_loader`; without that service its first restart hung it.
  Check how a SteamOS tool restarts, updates and stops itself before running it elsewhere.
- **Read a Decky plugin before installing it (2026-10-05).** decky-lsfg-vk would have installed
  an x86_64 library as an always-on Vulkan layer in the shared home directory, undoing the
  explicit-layer fix and loading a wrong-architecture library into every Vulkan program.
- **Vulkan layer order decides what a frame limiter counts (2026-10-05).** With lsfg-vk as an
  implicit layer above MangoHud, a 30 fps limit gave 15 real frames: MangoHud limited lsfg-vk's
  output, generated frames included. `VK_LOADER_DEBUG=layer` prints `Insert instance layer`
  from the driver upwards, which shows the chain. Explicit layers enabled from the environment
  are placed closer to the driver than the implicit ones, so lsfg-vk became explicit.
- **A benchmark is not a game (2026-10-05).** lsfg-vk's benchmark ran fine; in a game it failed
  at once, because only a real swapchain shares images between two Vulkan devices, and Turnip
  refused the mismatched import (`VK_ERROR_INVALID_EXTERNAL_HANDLE`). Test the real path early.
- **Mesa can say why a shader is slow (2026-10-05).** `IR3_SHADER_DEBUG=disasm` (with a fresh
  application cache, e.g. `XDG_CACHE_HOME=/tmp/...`, and `MESA_SHADER_CACHE_DISABLE=1`) dumps
  every Turnip shader with its statistics; `stp`/`ldp` instructions are register spills. That
  is how lsfg-vk's quality mode, 20-30 times slower than performance mode, was traced to
  spilling on the Adreno 650.
- **A game stuck on its loading screen can be a bad save, not the platform (2026-10-05).** Hue
  hung because its Steam Cloud save was an empty file; the Unity `Player.log`
  (`~/.config/unity3d/<company>/<game>/Player.log`) said so in plain words. Read the game's own
  log before suspecting FEX, Mesa or the kernel.
- **Old native ports can ignore a controller in-game although their menus see it (2026-10-05).**
  Outlast's Linux port did, with and without Steam Input. When the game's menus react to the pad,
  the platform side (device access, SDL mapping) is fine; try the Windows version through Proton
  before spending more time on the native port.
- **`apk add` failed although the package installed (solved 2026-10-05).** Making `/boot`
  read-only (audit C6) broke the postmarketOS initramfs trigger, which rewrites `/boot` whenever
  kernel, firmware or udev files change. apk then marked `postmarketos-mkinitfs` broken and
  counted it as "1 error" in every later transaction, so commands chained with `&&` never ran.
  Fixed with an apk commit hook (`armdeck-boot-rw`) that makes `/boot` writable only while apk
  works, plus one reinstall of the package. Lessons: a read-only mount needs a plan for every
  program that writes there; read apk's log (`/var/log/apk.log`) for the first failure, not the
  last "1 error"; and before letting a boot tool run, check whether it can flash a partition
  (boot-deploy can, with `flash_kernel_on_update`).
- **Sound card sometimes missing after boot (open, TODO 16).** "AFE failed to vote" in about 5 of
  17 boots; a reboot fixes it. The kernel fix already existed elsewhere: Nova-Deck's `0893` +
  `0895` (Philippe Simons, 2026) show the same log line for line and make `q6afe` wait for the
  DSP. We found it only through the systematic patch scan (docs/patch-scan.md), after working
  out the cause ourselves from 49 boot reports. Lesson: before working an open problem from scratch, search the
  related projects' patch notes for its exact log lines.
- **"Impedance detect ramp error" on the screen at boot (2026-10-10).** The phone has no 3.5 mm
  jack, but the device tree (the file that describes the hardware to the kernel) gives the WCD9380
  codec its headset detection (MBHC) with no USB-C audio switch, so the codec reads the
  unconnected detect pin as "headset plugged in" once when audio starts, tries to measure the
  headphone impedance, reads zeros and logs the error. Checked on the phone: one detection
  interrupt, not a loop; "Headphone Jack" and "Mic Jack" read "on", but the UCM profile (the ALSA
  description of the sound card) has only the speakers and the built-in mic, so output stays on
  the speakers; after detection the driver turns the mic bias off again (only the button-detect
  current source, microamps, stays on). Harmless. Earlier boot reports had up to 59 `wcd938x`
  lines per boot, and the boot report hid them as "a known problem" without anyone writing down
  why. It reached the screen because `quiet` still prints errors on the console;
  `20-armdeck-quiet-console.conf` (sysctl `kernel.printk = 3 4 1 7`) keeps everything below
  "critical" off the screen and in the logs. Lesson: a message hidden as "known" needs its
  cause written down, or the next person to see it starts from zero.

## Tools and workflow

- **`systemctl stop` cannot end a standby (2026-10-29, solved).** To let `op8-lowbatt` end a
  standby before it powers off, the first version ran `systemctl stop systemd-suspend.service`.
  Tested with a timer during a standby: systemd refused, "Transaction for
  systemd-suspend.service/stop is destructive (systemd-suspend.service has 'start' job queued)",
  because logind starts the suspend as an irreversible job. The standby went on until the power
  button. `systemctl kill -s TERM systemd-suspend.service` sends the signal without a job:
  `op8-standby` traps TERM, restores and exits, and the suspend job ends normally. Lesson: test
  the emergency path the way it will really be used, not only the normal one.

- **A logger started with `nohup ... &` over SSH died with the session (2026-10-29).** The
  first run of `audio-wake.sh` wrote only its header: when the SSH command ended, systemd
  stopped the session's scope and every process left in it, `nohup` or not. Started again with
  `systemd-run --user --unit=... sh script.sh`, it runs as a user service outside the session and
  survives. Use that for anything that must outlive an SSH command, and read the log once before
  asking the maintainer to start a measurement.

- **WiFi does not answer in standby (2026-10-29, explained).** SSH and ping time out while the
  phone is in standby; awake it works again. The cause is NetworkManager, not the screen: Steam's
  sleep request starts systemd's suspend target, NetworkManager takes WiFi down for the sleep
  and reconnects at wake-up ("sleep: wake requested" in the journal, then a new association).
  That saves power and stays. Measurements in standby have to start before it and must not
  depend on the SSH session.

- **A build script that follows someone else's master breaks on its own (2026-10-27).**
  `build-gamescope-op8.sh` fetched Alpine's gamescope files from aports `master`. By the next
  build master had moved to 3.16.31 and 3.17.0 (the script would have stopped on its version
  check), and gitlab.alpinelinux.org did not answer at all (curl exit 28). Now pinned to the
  aports commit of 3.16.29 through the GitHub mirror; the three Alpine patches are
  byte-identical to the ones used on 2026-10-02. Pin every outside source to a commit.

- **The phone's own boot.img is not the image we run (2026-10-27).** `install-kernel.sh` makes
  `/boot/boot.img` with the cmdline from the device package's `kernel-cmdline.conf`, which adds
  `clk_ignore_unused pd_ignore_unused` (unused clocks and power domains stay on). No image we
  ran ever had them: every test image since r7 was repacked on the PC from the previous one's
  header and cmdline (`r14/mkimg.sh`). Writing the phone's boot.img would have changed two
  things at once, one of them untested and costly for the battery. Test images stay repacked
  (`r16/mkimg.sh`: r16 kernel and DTB, r14 ramdisk, header and cmdline, checked byte for byte);
  whether the device package should drop the two options is still open.

- **WSL wipes `/tmp` between calls.** Work under `/mnt/d`.
- **`sed` rewrote CRLF in a patch file.** Patch files are binary-exact: edit them with byte-level
  tools and recompute the APKBUILD checksums.
- **A time threshold skipped the session-start source check (2026-10-05).** The check ran with
  `--if-older-than 20`; the new session began 19.8 h after the last one, so it was skipped, and
  13 new DroidDeck commits (Decky, frame generation, .NET games under FEX) went unseen until the
  maintainer asked. The check now runs at the start of every session, with no threshold.
- **BusyBox tools differ.** No `pgrep -c`, `grep --line-buffered`, `ps -p` or `ls --time-style`;
  check the BusyBox usage text before relying on GNU options.
- **Deployed copies drift from the repository (checked 2026-10-05).** The phone still ran the
  pre-translation copies of 14 scripts and services (Romanian log messages and unit
  descriptions); stripped of strings and comments, the code was identical everywhere, so nothing
  behaved differently. Method: hash every `userspace/` file, find copies on the phone by name,
  then diff without comments and quoted strings. Fixed the same day with
  `userspace/deploy-manifest.txt` and `tools/deploy.sh` (check, then stage + `install.sh` with
  backups): 22 files redeployed, two old-named rules (udev, polkit) moved away, everything "same".
  **It happened again (found 2026-10-30):** the phone's `steam-in-container.sh` was the copy of
  2026-10-06, without two changes committed since (the Remote Play preload of 2026-10-11 and
  `DOTNET_EnableWriteXorExecute=0` of 2026-10-29), and `op8-remoteplay` with its source was not
  on the phone at all. Deployed that evening; `op8-remoteplay` built the preload at the next
  Steam start. Lesson: run `tools/deploy.sh check` after every change under `userspace/`, not
  only before a planned deploy.
- **Restarting a user service during a kernel lock hang makes it worse.** `systemctl` waits on the
  stuck manager; use `sudo sync && sudo reboot -f`, or the button reset.

## UTDeck (Ubuntu Touch on Halium)

Problems met while building UTDeck on the Redmi Note 12 5G (stone). Details in `docs/ut-deck/`.

- **Lomiri's screen-off state changes Vulkan presentation (open, stage 0).** With the screen off,
  `vkcube` in FIFO settled at exactly 30 fps and MAILBOX stalled for the whole 120 s run; the
  vendor driver through libhybris also stalled. Lesson: measure frame rates with the screen on and
  unlocked; with the screen off only CPU cost per frame is meaningful.
- **`vkcube` ignores `--width`/`--height` on Lomiri (stage 0).** The phone shell sizes every window,
  so a "1280x720" run cost the same as a full-screen one. Lesson: record the window's real size,
  not the requested one.
- **Mesa's build tried to write into a read-only source tree (stage 1.1).** meson unpacked a
  fallback copy of `libarchive` (wanted only by freedreno's debug tools) into `subprojects/`,
  which the container mounts read-only, and stopped with "Unhandled python OSError".
  `--wrap-mode=nodownload` did not help; `--wrap-mode=nofallback` did. Lesson: build Mesa with
  `nofallback` and list every dependency in the container explicitly.
- **Two builds of the same Mesa commit are not bit-for-bit identical (stage 1.1).** 1,177 bytes of
  generated code differed, all strings equal. Lesson: test the binary the script produces from a
  clean clone, and record its SHA-256, rather than a local build made another way.
- **`vkcube` on libhybris' Vulkan shows nothing on Lomiri (stage 0).** libhybris reports the
  surface as 1x1 (`currentExtent`), `vkcube` trusts it, and asks for 1x1 buffers; Lomiri shows an
  empty "Unknown" app. Known from dolphin-ut's stage 0. Lesson: test libhybris' Vulkan presentation
  with a program that uses the window's own size, not `currentExtent`.
- **A manual test silently reused the previous test's settings (stage 0).** M3 was meant to run the
  vendor driver, but M2's `export` of `LD_LIBRARY_PATH` and `VK_DRIVER_FILES` was still active in
  the same shell, so it ran Turnip again; only the "Selected GPU" line gave it away. Lesson: every
  manual test starts with `unset` of the variables earlier tests set (or asks for a new shell), and
  says which output line proves the right driver ran.
- **Turnip's software presentation shows a torn picture in portrait (open, stage 0).** Mesa rounds
  each row up to 64 bytes (stride 4352 bytes for 1080 pixels) and passes the stride over `wl_shm`;
  Lomiri (Mir 1.8) most likely ignores it. Landscape (2400 wide, stride 9600 = 2400 x 4, no padding)
  looked normal. Lesson: when only one
  orientation breaks, compare the width with the row stride first.
- **Turnip lost the GPU under Geekbench's Vulkan compute test (open, stage 0).** On the Adreno 619
  both DD-Turnip v0.7.0 (Mesa 27.0, Linux) and Turnip v25.3.0 R11 (Android, through libhybris) gave
  a wrong result in one workload, then KGSL logged "gpu timeout" and dropped the context
  (`VK_ERROR_DEVICE_LOST`); the vendor driver passed. KGSL kills command buffers that run over 2 s
  (`dispatch/drawobj_timeout`). A driver that runs games fine (R11 in Dolphin) can still fail at
  compute. Lesson: before blaming a build or a loading route, test a second build by the other
  route; and run a compute test on every Turnip, not only games.
- **"Best of 3" hid the GPU's power-up and produced a false 5-8 ms submission cost (stage 1.2,
  corrected in 1.4).** The first dispatch after the GPU has been idle takes milliseconds on both
  drivers, by a varying amount; three dispatches were not enough to get past it. Lesson: for
  latency, run 15 or more and read the steady state, and split the time (submit, wait, GPU time
  from timestamp queries) before drawing a conclusion.
- **Mesa's EGL could not reach zink on a GPU without a DRM render node (stage 1.3).** KGSL has no
  render node; EGL's only path without one is its "software" branch, which can load zink, but the
  fallback cleared the zink request before taking it; and `LIBGL_ALWAYS_SOFTWARE=1` makes zink look
  for a CPU Vulkan device. Fixed by keeping an explicit `MESA_LOADER_DRIVER_OVERRIDE=zink` (our patch
  0003). Lesson: on Halium phones, read Mesa's EGL fallback chain before assuming a missing driver.
- **Geekbench's "wrong results" on Turnip were Geekbench's own invalid work-group size (stage
  1.2).** Its tuner used 2048x1x1, over `maxComputeWorkGroupSize[0]` (1024), apparently checking
  only `maxComputeWorkGroupInvocations` (2048 on Turnip, 1024 on the vendor driver, so the vendor
  driver never saw it). Found by recording the real dispatches with a Vulkan layer of our own and
  replaying all of them on both drivers. Lesson: when every shader passes alone, record and replay
  the program's own dispatches; and check an application against the specification before calling
  a difference a driver bug.
- **Turnip packs several command buffers into one KGSL submission, and KGSL's 2 s limit is per
  submission (stage 1.2).** Geekbench's blur tuner passes five command buffers per `vkQueueSubmit`;
  the vendor driver submits each separately (each retires and restarts the timer), Turnip sent one
  submission of five and hit "gpu timeout". One submission per command buffer let the whole
  benchmark finish. Lesson: trace both drivers' ioctls (`kgsl-trace`) before guessing; the
  difference was in packaging, not speed.
- **A long Turnip job froze the phone's UI because everything shared one GPU queue (stage 1.2).**
  KGSL gives contexts without a priority the medium one and preempts only between its four
  priority queues; Turnip on KGSL asks for no priority (and ignores Vulkan's), so a 0.8 s dispatch
  made a compositor-like job wait 755 ms. With Turnip's context at priority 12 the wait fell to
  2.3 ms. Lesson: run games below the compositor's GPU priority, and measure UI-side latency, not
  only the game's speed.
- **A test of a shader with no bounds checks gave "driver differences" that were our own fault
  (stage 1.2).** Geekbench's unchecked matrix multiply read past its buffers at our non-multiple
  sizes, and the two drivers left different data there. Lesson: before calling a difference a
  driver bug, check the shader's bounds checks against the sizes used.
- **"Slower driver" was the wrong guess for a GPU timeout (stage 1.2).** Turnip ran Geekbench's blur
  15 % faster than the vendor driver, yet only Turnip hit KGSL's 2 s limit. Lesson: measure the
  single dispatch first; a timeout per submission can come from how work is batched, not from the
  shader's speed.
- **Qualcomm's V@530 Vulkan driver gets `OpSMod` wrong with mixed signs (stage 1.2).** It returns
  C's remainder (sign of the dividend) instead of SPIR-V's modulo (sign of the divisor): 6.7
  million wrong results in our `intops` test, where Turnip had none. Lesson: a test that "fails on
  Turnip, passes on the vendor driver" does not make the vendor driver the reference; check both
  against a CPU reference that follows the specification.
- **`adbd` crashes and the USB gadget does not come back (open, stage 1.2; stone-ut).** Twice,
  at the start of an adb session right after an `adb push`, `adbd` died with SIGSEGV; systemd
  restarted it, but it logged `timed out while waiting for FUNCTIONFS_BIND` and the phone stayed off
  USB until the cable was replugged. It looked like a hardware disconnect from the PC. Lesson: when
  adb vanishes, read the phone's journal for `adbd.service` before suspecting the cable.
- **Long benchmark runs die when `adbd` restarts (stage 0, corrected in stage 1.2).** Turning the
  screen on, or replugging the USB cable, makes systemd restart `adbd`, and systemd stops every
  process in `adbd.service`'s cgroup. `setsid nohup ... &` does **not** escape that: it leaves the
  session, not the cgroup (a run started that way died at a replug in stage 1.2). Lesson: start
  long runs as a user unit, `systemd-run --user --unit=NAME --collect CMD` (with
  `XDG_RUNTIME_DIR=/run/user/32011` and the session bus address), and poll a status file.
- **The phone has no `bc` and no GNU `time` (stage 0).** Timing a command in an `adb shell` failed
  silently. Solved with `tools/ut-probe/run-timed.py` (wall time, CPU time, fps).
- **Wine on 39-bit address space kernels (avoided, stage 0).** stone's kernel has
  `CONFIG_ARM64_VA_BITS=39`; Wine's preloader linked at a 47-bit address dies before starting
  (Wine bug 59898, fixed in 11.15). Lesson: read `CONFIG_ARM64_VA_BITS` on every new device, and
  use Wine 11.15 or later, or a `-static-pie` build.
- **Paced frames at half the panel rate while the GPU idles (worked around, stage 1.4).** On Lomiri at
  120 Hz, zink on our Turnip ran 44 fps with swap interval 1 and 208 fps with interval 0; the
  vendor GLES driver 84 and 275; the GPU under 40 % busy in every run. The loss is the wait between
  frames, not rendering: Lomiri lacks `wp_fifo_v1`, so Mesa's Wayland WSI waits for the
  compositor's frame callback before each present, and that answer comes late. Lesson: before
  hunting for GPU speed, compare paced and unpaced runs; if unpaced is fast, the problem is pacing.
  `MESA_VK_WSI_PRESENT_MODE=mailbox` plus a frame limiter is the fallback. Time-stamping the
  Wayland messages (`WAYLAND_DEBUG=client`) then split it in two: Lomiri sends "done" 12-15 ms after
  each commit for every client, and zink's EGL waits for "done" before it sends the frame to the
  GPU, so drawing and waiting add up instead of overlapping. Lesson: `WAYLAND_DEBUG` timestamps
  show who waits for whom, without any special tool. Solved by not waiting at all: swap interval 0
  (MAILBOX, no tearing under Lomiri) plus a frame limiter gives a steady 60 fps (59.6-59.9 on the
  panel) where waiting for vsync gave 44; flushing earlier only reached 53.5.
- **A benchmark gap is not always a driver bug, in either direction (stage 1.4).** Geekbench's
  Particle Physics ran 41 % slower on Turnip and Stereo Matching 7.6 times faster. Recording the
  real dispatches with our layer and replaying them on both drivers settled both: the same GPU
  clock and submissions, Turnip's compiled particle shader is 1.6 times slower (a compiler issue for
  upstream), and the stereo shader gives identical output 8 times faster on Turnip (a vendor slow
  path). Lesson: replay the exact dispatch on both drivers with the same inputs and compare time
  and output before calling it a bug or a win. The vendor driver cannot take a Vulkan layer on
  Ubuntu Touch (libhybris' `libvulkan.so.1` is not the Khronos loader), so replay is the way to
  measure it.
- **Route A's Vulkan presentation hangs when the swapchain is recreated (stage 1.4).** With the
  Android Turnip (R11) through libhybris, `vkcube` presented two frames, then stopped for good after
  Lomiri's second `configure` (the window becoming active), when it recreates its swapchain. Same
  hang dolphin-ut met (Dolphin rebuilds the whole surface instead). Wine recreates swapchains on
  every resize, so this weighed heavily against route A.
- **A driver can advertise a feature the kernel cannot back (stage 1.4).** R11 reports sparse
  binding, but Turnip implements it on KGSL with virtual buffers (`KGSL_MEMFLAGS_VBO`), which
  stone's kernel lacks; our 26.2.5 build probes the kernel and leaves it out. Lesson: compare a
  feature against the kernel source before trusting `vulkaninfo`, especially for VKD3D-Proton.
- **Wine on Ubuntu Touch loads libhybris unless told otherwise (stage 2.1).** The first `wineboot`
  failed with "Failed to load hybris linker ... cannot allocate memory in static TLS block": the
  system's `libvulkan.so.1` and `libEGL.so.1` lead into libhybris, and Wine opens them while
  starting. Solved by giving Wine our driver environment (Khronos loader and our Turnip, our Mesa
  as the EGL vendor). Lesson: on Halium, every process that may touch the GPU needs the driver
  environment set, even a "console only" one.
- **A Wine prefix can be 2.3 GB (stage 2.1).** Wine copies its Windows DLLs into each prefix, and
  Hangover's DLLs keep their debug information (three times the stripped size). Lesson: strip our
  runtime's DLLs and share prefixes or DLLs between games before shipping.
- **Timing a Wine program through a pipe measures `wineserver` too (stage 2.1).** `wineserver`
  inherits the output pipe and stays a few seconds after the program ends, so a tool that waits
  for the pipe to close reported 7 s for a 0.7 s run. Lesson: time Wine programs with output to a
  file, or wait for the process, not the pipe.
- **Wine's Wayland driver has no touch input (stage 2.2).** `winewayland` (Wine 11.16) binds the
  pointer and keyboard but never `wl_touch`, so on a phone no tap reaches a Windows program. Our
  patch 0002 makes the first finger act as the mouse. Lesson: check every input path a phone uses,
  not only the display, before calling a desktop component "working".
- **Wine's configure finds no libraries in a cross build without a cross `pkg-config` (stage
  2.2).** It looks for `aarch64-linux-gnu-pkg-config`, silently disables Wayland when it is
  missing, and the only trace is "--cflags: command not found" in `config.log`. Solved with
  `PKG_CONFIG=pkg-config` and `PKG_CONFIG_LIBDIR` pointed at the arm64 libraries. Lesson: after a
  cross configure, check that the features you need were found before building.
- **Under Wine, a Windows program's output into a pipe is fully buffered (stage 2.3).** A test
  program killed by a timeout showed nothing, as if it had stopped before its first line. Lesson:
  test programs call `setvbuf(stdout, NULL, _IONBF, 0)` first.
- **FIFO presentation can deadlock a Wine game on Lomiri (stage 2.3).** Mesa's Wayland WSI, in FIFO
  without `wp_fifo_v1`, waits in `vkQueuePresentKHR` for the frame callback; Lomiri holds that
  callback until the window acknowledges a new size, which Wine handles on the same (blocked)
  thread. It depends on timing (one windowed run passed, the next hung). MAILBOX never waits and
  never hung: UTDeck forces it (`MESA_VK_WSI_PRESENT_MODE=mailbox`). Lesson: a run that passes once
  proves little when the failure depends on the order of two events; repeat it.
- **Lomiri never tells Wine that the screen rotated (stage 2.3).** Mir 1.8's `wl_output` stays
  1080x2400 with no transform; the only sign of a rotation is the size Lomiri asks for in a
  window's `configure` (2400x1080). Wine sizes its monitor from `wl_output`, so a fullscreen game
  stayed portrait in a landscape window (picture on the left 45 %). The Dolphin port met the same
  (its stage 6.6). Lesson: on Lomiri, take orientation from the window's configured size, never
  from the output.
- **Lomiri's launcher gives apps an X11 display, and Wine then picks X11 (stage 2.3).** Started from
  a desktop file, `DISPLAY=:0` (Xwayland) was set, Wine used its X11 driver, and Vulkan failed with
  `VK_ERROR_EXTENSION_NOT_PRESENT` (our Turnip presents only to Wayland). UTDeck's launcher unsets
  `DISPLAY` (or sets Wine's graphics driver to `wayland`).
- **Wine's background processes keep a Lomiri app "running" (stage 2.3).** `wineserver` and
  `winedevice.exe` stay in the app's process group after the program exits, so Lomiri refused to
  start the app again until systemd killed them a minute later. UTDeck ends them (`wineserver -k`)
  when a game exits.
- **Lomiri can shrink a fullscreen window to the size without its top bar (stage 2.3).** For apps
  it manages (started from the drawer), it granted "fullscreen" at 2400x1017, with the bar over
  the game. The Dolphin port met the same (its stage 5) and asked for fullscreen again once the
  size was stable; our `winewayland` patch 0004 does the same for every game, and the bar goes
  within a second or two. A first version that asked again at once, inside the `configure`
  handling, never helped. Lesson: when a fix for Lomiri already exists in the Dolphin port, copy
  its timing too, and test it on screen (with the screen off Lomiri never gave the full size).
- **This phone's `adb shell` always exits 0 (stage 2.4).** `adb shell false` returns 0, so a PC-side
  wait loop on `adb shell 'grep -q ...'` ended at once while the phone's installer was still
  running. Lesson: wait on the command's output (`grep -c` and compare the text), not on adb's exit
  status.
- **Box64 stalls on a self-unpacking 32-bit DLL where FEX works (stage 2.4).** 3DMark 11's
  InstallShield setup loads `ISSetup.dll`, packed with PECompact2; under Hangover's default 32-bit
  emulator (Box64) it never got further, under FEX (`HODLL=libwow64fex.dll`) the wizard ran. Lesson:
  when a 32-bit program hangs without an error, try the other emulator before anything else.
- **InstallScript installers do not install silently without an answer file (stage 2.4).** `/s`
  made 3DMark 11's setup wait forever. Record the answers once in a normal run (`setup.exe /r
  /f1C:\file.iss`), then `/s /f1...` replays them. And never put a short time limit on a run that a
  person may be clicking through.
- **Old game installers bundle Microsoft runtimes that do not work under Wine (stage 2.4).**
  3DMark 11's setup ran Microsoft's .NET Framework 4 installer for ten minutes and then hung at its
  end. Wine provides these itself (Wine Mono for .NET, built-in `d3dx`/`d3dcompiler` for DirectX);
  Visual C++ runtimes do install. UTDeck should install Wine Mono in every prefix and, where it can,
  skip bundled .NET and DirectX installers (Proton and DroidDeck do the same).
- **Wine's COM does not find 32-bit out-of-process servers from 64-bit programs (stage 2.4).**
  Futuremark SystemInfo registers its COM service only in the 32-bit registry view; 64-bit Windows
  lets a 64-bit client start it, Wine did not ("class not registered"). Copying the class key into
  the 64-bit view, adding `LocalService` to its AppID and writing the missing service key fixed it.
  Lesson: when a 64-bit program says "class not registered", look for the class under
  `Wow6432Node\CLSID`.
- **Wine's win32u replaces a swapchain's pNext chain when it adds present scaling (stage 2.4).**
  Under an emulated display mode Wine sets `create_info_host.pNext = &scaling`, dropping DXVK's
  `VkImageFormatListCreateInfo`; Mesa then read a NULL format list and every mode-changing D3D game
  crashed in `vkCreateSwapchainKHR` (3DMark 11). Found by mapping the fault address through the
  process's `/proc/<pid>/maps` to `libvulkan_freedreno.so` and `addr2line` on the unstripped build.
  Lesson: "Exception in Unix call" can be found to the source line with the process's memory map
  and an unstripped build; no debugger needed.
- **A configure option that is harmless for one library can strip another (stage 2.4).**
  `--without-freetype` did not matter for `winewayland.so` but removed font rendering from
  `win32u.so`. Lesson: compare a rebuilt library's imports with the original's before using it
  (`nm -D --undefined-only`), and make the build fail if a needed feature is missing.
- **A stuck program's results can be read from its memory (stage 2.4).** 3DMark 11's GUI waited
  forever for SystemInfo's hardware scan (it spins without its CPU-Z driver under Wine), but the
  test results were already in its memory as XML; `memgrep.py` reads `/proc/<pid>/mem` (same user,
  no Yama restriction on this kernel) and found them. Lesson: before killing a stuck process,
  look in its memory; and kernel-driver based hardware scanners never work under Wine, so skip them.
- **`pgrep -f NAME` also finds the shell that runs it (stage 2.4).** A wait loop on
  `adb shell 'pgrep -fc scaletest-app.sh'` never ended: the count includes the `bash -c` holding
  that very string. Lesson: use `pgrep -f '[s]caletest-app.sh'` (the bracket keeps the pattern from
  matching itself).
- **Wine's configure leaves out quietly what it cannot find, and a test that does not use a
  feature will not miss it (stage 3.1).** Our first full Wine build came from a container that had
  only the arm64 libraries the Wayland driver needed: no PulseAudio, SDL2, udev, D-Bus, GnuTLS or
  GStreamer, so no `winepulse.so` (no sound), no SDL gamepads in `winebus`, no `winegstreamer`, no
  TLS. Every console and graphics test passed. A `winebth` error that Hangover did not have gave it
  away; comparing the list of Unix-side modules with Hangover's showed the rest. Fixed by
  installing Hangover's build dependencies (minus X11 and what the phone lacks); the build script
  now fails if configure reports any needed feature missing or one of the key modules was not
  built, and lists in `BUILD-INFO` what was left out. Lesson: when replacing a working build,
  compare the file lists, not only the test results.
- **Installing arm64 `-dev` packages can replace the container's own Python (stage 3.1).**
  `libglib2.0-dev:arm64` (pulled in by the PulseAudio, SDL2 and GStreamer headers) depends on
  "python3 | qemu-user | qemu-user-static"; apt chose arm64 Python, which replaced the amd64 one and
  failed to run ("Exec format error"). Installing the amd64 `qemu-user-static` first satisfies the
  alternative. Lesson: in a multiarch cross container, check `apt-get install -s` for `Inst python3`
  of the foreign architecture before installing.
- **A PE linker stamps the build time into every file (stage 3.1).** Two clean builds of the same
  runtime differed in 1,586 of 1,799 files, by four bytes each: the PE header and debug directory
  time stamps. llvm-mingw's linker uses `SOURCE_DATE_EPOCH` when it is set; with the Wine commit's
  time there, two clean builds are identical in all 1,812 files. (A test DLL still differed, but
  only because the two copies had different file names, which the export table contains.)
- **FEX's version string becomes the emulated CPU's name, 31 characters at most (stage 3.1).**
  A longer `OVERRIDE_VERSION` stops the build at a `static_assert` in `CPUID.cpp`.
- **An old release may not compile with a newer toolchain, while its upstream already has the fix
  (stage 3.1).** DXVK 2.7.1 fails with LLVM 23's libc++ (a missing `<algorithm>`, and an
  `unordered_map::emplace` with an empty tuple); both were fixed upstream after the release. We
  carry the two upstream commits as patches instead of changing the pinned version or the
  toolchain.
- **`adb push` does not keep a script executable unless the source file is (stage 3.1).** A
  pushed `rt-gfx.sh` replaced the phone's executable copy and the user unit failed with status 126
  (no permission). The repository's scripts are now executable, and tests check the unit's journal
  when a log does not appear.
- **Look for DRM bypasses before the first start, not after (stage 3.2).** Three game copies
  offered for the compatibility table turned out to carry cracks: a Steam emulator
  (`steam_emu.ini`, a replaced `steam_api.dll`), a no-DVD executable (the marker `*!ReLOADeD!*` in
  `speed.exe`) and a crack DLL for a publisher's DRM (`kDays.dll`, "Universal Cracker Kernel"). One
  of them was found only after it had been tested, and its results were withdrawn. UTDeck does not
  test games through DRM bypasses. Lesson: before copying a game to the phone, check its folder
  (`steam_emu.ini`, `*.cdx`, unexpected `steam_api.dll`, crack DLLs) and run `strings` on its
  executables and DLLs for group names and words like "crack".
- **Ubuntu Touch's session sets variables that steer other programs (stage 3.S).** Every app gets
  `SDL_VIDEODRIVER=wayland,x11`, `XDG_SESSION_TYPE=mir`, `QT_QPA_PLATFORM=...` and a glibc tunable
  (`glibc.rtld.optional_static_tls=4096`, which libhybris programs need). An SDL3 built without
  Wayland stops at the first name instead of trying the second; a libhybris tool run from adb
  (`mirscreencast`) fails with "cannot allocate memory in static TLS block" without the tunable.
  Lesson: read `systemctl --user show-environment` before guessing why a program behaves
  differently from adb and from the app drawer, and set such variables outright, not with `${X:-}`.
- **Valve's ARM64 Steam client assumes the Steam Frame (stage 3.S).** Its web helper asks SDL3 for
  Wayland whatever the environment says, its bundled SDL3 has no Wayland, its start script sets
  `LIBGL_KOPPER_DISABLE=true` and pins it to CPUs 2-6, and it expects its parts to run in the Steam
  Runtime. Each of these had a different fix (our own SDL3 at Valve's commit, Mesa patches 0003 and
  0004, the runtime's tools in `PATH`). Lesson: with a build made for one device, list what that
  device provides (compositor, runtime, CPU layout) and check each against ours, instead of fixing
  errors one at a time as they appear.
- **Killed Steam runs leave shared memory behind (stage 3.S).** `/dev/shm` (RAM) held 1.1 GB of
  `u<uid>-Shm_*` segments after a day of test runs. The start script now removes them when no Steam
  process is left. Lesson: after killing a program in tests, check `/dev/shm` as well as processes.
- **The phone's screen can be checked from the PC (stage 3.S).** `mirscreencast` with the session's
  glibc tunable gives a raw RGBA frame (`tools/ut-probe/screenshot.sh`). Many "does it show?"
  questions no longer need the maintainer.
- **SDL3 ignores SDL_VIDEODRIVER on Ubuntu Touch (stage 3.S).** `SDL_IsUbuntuTouch()` (true when
  `XDG_SESSION_DESKTOP=ubuntu-touch`) makes SDL3 always use Wayland. A program whose SDL has no
  Wayland, or which needs X11 handles (Steam's web helper), fails with misleading errors. Unset
  `XDG_SESSION_DESKTOP` for such programs. It took a day of other theories to find, because the
  logged error ("Invalid window") was a stale one. Lesson: when a library seems to ignore its
  configuration, read the library's source for platform special cases before testing more
  settings; a disassembler on the caller then shows what it really needs.
- **Steam on ARM64 would emulate all of Proton (stage 3.S).** The ARM64 client downloads the
  x86-64 Proton and runs it, Wine included, through FEX inside Steam Linux Runtime 4: it adds FEX
  under every tool named `SteamLinuxRuntime_*` (not `-arm64`), and Proton requires SLR 4. A custom
  compatibility tool whose `toolmanifest.vdf` requires nothing gets neither the container nor FEX.
  Lesson: before replacing a vendor's chain, read its manifests and the client's rule for chaining
  them (here in `steamclient.so`, next to its debug messages).
- **Steam gives install scripts paths relative to its own folder (stage 3.S).** The install script
  is started as `iscriptevaluator.exe legacycompat\evaluatorscript_<appid>.vdf` from Steam's folder.
  Our launcher changed into the program's folder first, so the script was not found (exit 1).
  Lesson: a launcher keeps the working folder it is given, as Proton does; Steam already sets it to
  the game's folder for games.
- **`echo` of the phone's `sh` reads backslashes (stage 3.S).** `legacycompat\evaluatorscript...`
  was logged as `legacycompatvaluatorscript...` (`\e` became an escape character), which hid the
  real argument. Lesson: in sh scripts, print paths and arguments with `printf '%s\n'`, never `echo`.
- **A wrapper returned the wrong exit code (stage 3.S).** `game-run.sh` ended with `wineserver -k`,
  so every run looked like exit 0 to Steam and to our log, while the program had returned 1 or 53.
  Lesson: save `$?` right after the command that matters and `exit` with it at the end.
- **Box64 spins in the Visual C++ 2015 installer; FEX runs it (stage 3.S).** Steam's redistributable
  step started `vc_redist.x86.exe /q`; its unelevated child (WiX "burn" engine) used one core at
  100 % for 10 minutes without progress under Box64 0.4.4 (Wine's default for 32-bit code). Under
  FEX (`HODLL=libwow64fex.dll`) the same installer finished in 8 s. Our tool now runs Steam's
  install scripts under FEX. Lesson: when 32-bit code hangs at full CPU, try the other emulator
  before debugging Wine; which one games get is a measurement, not a guess.
- **Steam passes Steam Frame settings to every game (stage 3.S).** `VK_INSTANCE_LAYERS` names two
  Vulkan layers this phone does not have (the loader skips them, with a warning) and `TU_DEBUG_FILE`
  lets a file change Turnip's debug options while a game runs. Our tool clears both. Lesson: log
  the environment a launcher receives (names only where values can hold account data, as
  `SteamUser` does) before trusting it.
- **The phone dropped off USB during work (stage 3.S, 2026-11-19).** adbd logged
  `FUNCTIONFS_SUSPEND` then `DISABLE` (the PC side stopped the link); the PC's kernel then logged a
  reconnect with `error -71` (a USB protocol error) and another drop 21 s later. The phone had not
  rebooted (it was up for a day); the maintainer restarted it and it has worked since. Cause still
  open: the cable or the PC's port are the first suspects, not Steam. Lesson: when adb disappears,
  check `journalctl -k` on the PC and adbd's log on the phone before suspecting the last command.
- **Proton's Steam bridge is half in Proton, half in Proton's Wine (stage 3.S).** `lsteamclient` and
  `steam.exe` build from Proton's tree, but a game reaches them only because Proton's Wine loader
  turns Valve's Windows `steamclient.dll` into jumps to `lsteamclient`; without that (our Wine,
  AndreRH's), the game loads Valve's DLL, finds no Steam and quits (Ys: exit 53). Ported as Wine
  patch 0009. Lesson: when taking one component from a project, search its other repositories
  (here its Wine fork) for the code that calls it.
- **Wine's makedep cannot build a module from outside the tree (stage 3.S).** Proton's Wine has a
  makedep change for that; ours crashes (no `include` among the folders) or wants every IDL
  dependency. A copy of the build tree made of links, with the module added, and `make` asked for
  only the module's files, builds it without touching Wine's own tree. Check with `make -n` which
  folders a build would write before running it on a shared tree.
- **A hidden window still costs CPU (stage 3.S).** Steam's main window opened on the invisible
  Xvfb screen when a game was started with `steam -applaunch` (which brings the window up, as a
  second start of Steam does); its Store page played trailers, composited in software by Mesa's
  llvmpipe: 200-280 % CPU while the game ran, unseen. Found by listing the windows on that screen
  (`xwin.py`, through libX11 with ctypes, as the phone has no X tools) and fixed by closing the
  window. Lesson: when a background program costs CPU, look at what it draws, even where nobody
  can see it; thread names (`llvmpipe-N`, `Media`) point at the cause.
- **Steam account numbers appear in paths (stage 3.S).** Steam's folders for an account are named
  by its number (`userdata/<n>`, `Steam Controller Configs/<n>`), so log lines and file lists can
  show it. Filters now also mask numbers of 9 digits or more.
- **This phone's `adb shell` always exits 0 (stage 3.S).** Wait loops written as
  `until adb shell 'pgrep ...'` stopped at the first try, so some waits looked as if programs had
  been running for less time than had passed. Already noted in `runtime-push.sh`, missed again.
  Lesson: over adb, decide on the command's output (`[ "$(adb shell '... | grep -c X')" -gt 0 ]`),
  never on its exit code.
- **Lomiri decides a window's orientation, and may stop rotating it (stage 3.S).** A fullscreen
  window from a background service (a Steam game) got 1080x2400 although the phone was sideways,
  and then no configure at all when the phone turned; started upright, every turn arrived. A
  program cannot rotate itself on Lomiri: it can only follow the sizes Lomiri sends. Lesson: test
  orientation from both starting positions, and note how the program was started (a Lomiri app
  with a desktop file, or not).
- **Do not let the compositor resize a game's window behind its back (stage 3.S).** Patch 0003
  resizes a fullscreen window to follow a rotation, which is right for games that fill the
  screen at the panel's resolution; for a game shown scaled (fill mode) it turned its 1200x540
  window into 1080x2400: a portrait picture and four times the pixels. Lesson: when Wine shows a
  window differently from its size, keep the window's size and change only how it is shown.
- **A black world with a working HUD can be the game itself (stage 3.S).** Ys went black in the
  middle of play, only the HUD left, with no error and the GPU as busy as before. The logs ruled
  out the picture's way to the screen; the game's forum history showed the same bug on Windows,
  tied to controllers; the phone's journal showed the controller had connected after the game
  started. Starting with the controller already on fixed it. Lesson: when only part of the picture
  is wrong, check what is still drawn (HUD, menus, a model) and look up the game's own known bugs
  before changing the runtime; and note what changed just before (here a controller waking up).
- **Wine lists GStreamer plugins inside the game (stage 3.S).** Wine starts GStreamer with
  `--gst-disable-registry-fork`, so a new or changed plugin is loaded into the game's own process
  to be listed; on Ubuntu Touch the hybris plugin then ends the process (it needs a glibc setting
  Wine's environment does not have). Giving Wine its own plugin list turned a hidden risk into a
  crash at the first movie. Lesson: build GStreamer's list outside Wine before it starts, and know
  which system plugins a Wine process can reach.
- **A sound that repeats its last second means the stream stalled, not the sound (stage 3.S).** The
  sound card replays its buffer when nothing new arrives. Here the video renderer hung, which held
  the whole movie. Lesson: on a looping sound, look for the thread that stopped (a trace of the
  media path showed the video thread stuck in the renderer's blit), not at the audio decoder.
- **A file's name does not tell its container (stage 3.S).** Ys's `op.wmv` is an AVI file; the
  other two `.wmv` are ASF. Wine handles the two very differently. Lesson: read the first bytes
  (`RIFF`, or the ASF header GUID `30 26 b2 75`) before reasoning about a media file.
- **Sound without a media role escapes the volume keys (stage 3.S).** On Ubuntu Touch the keys
  change a role's volume (`multimedia`, ...); Wine's streams had no role and played at a fixed
  0 dB. `PULSE_PROP='media.role=multimedia'` fixes every Wine program at once. Lesson: check a new
  sound source with `pactl list sink-inputs` (role, restore id, volume) and the volume keys.
- **Steam's default compatibility tool skips games with a Linux build (stage 3.2b).** Goat
  Simulator came down as its native Linux port, which on this phone would run entirely under FEX
  and outside our runtime. Steam's "all other titles" mapping (app "0") covers only Windows-only
  games; a per-game mapping (`set-default.py --app`) makes Steam swap to the Windows depots.
  Lesson: after each download, check which platform's files arrived before planning the run.
- **Turning a DLL off breaks every program that imports it (stage 3.2b).** `mscoree=` (no .NET)
  was a default to save time; Goat Simulator imports `mscoree.dll`, so Windows' loader refused to
  start it (`c0000135`, Steam's exit 53). Lesson: a default may turn off what programs call at run
  time, never what they import; read the imports first (`gamecheck.py`).
- **A sourced script can overwrite your variables (stage 3.2b).** `env.sh` sets `P` and `W`; a
  command that set `P` to a prefix path and then sourced `env.sh` installed Wine Mono into the
  tools folder instead. Lesson: source `env.sh` first, and use names it does not set (or check
  `WINEPREFIX` before running wine on it).
- **An emulator can hide the CPU's features from Windows programs (stage 3.2b).** Under Box64,
  `IsProcessorFeaturePresent` answered "no MMX, no SSE" in every 32-bit program, because Box64's
  WoW64 module at our pin lacked the export Wine asks (`BTCpuIsProcessorFeaturePresent`); `cpuid`
  itself was right. Microsoft's XAudio2 refused to start; other code silently took slower paths.
  Lesson: check what a program is told about the CPU with a small test under each emulator, and
  look upstream for the fix before writing one.
- **Check what upstream fixed since the pin before debugging the emulator (stage 3.2b).** A 64-bit
  game crashed in a network thread with an address inside Wine's syscall dispatcher; FEX had changed
  its ARM64EC system calls three times in the three months after Hangover's pin (one: syscall
  numbers hard-coded from Windows). Upstream's release fixed it in one 43 s build. Lesson: for a
  crash at the emulator boundary, list upstream's changes since the pin and A/B the newest release
  first; pinning "what Hangover ships" is a start, not a reason to stay behind.
- **Steam's redistributable installers can leave a broken mix (stage 3.2b).** The Visual C++ 2019
  setup kept Wine's `msvcp140` (it claims a newer version) and put Microsoft's older `vcruntime140`
  under it; under ARM64EC that pair crashed at load. The DirectX June 2010 setup put Microsoft's
  XInput DLLs in, which cannot work under Wine. Lesson: after a game's install script, check which
  of Wine's DLLs were replaced and whether the result is consistent (`gamecheck.py`-style version
  and builtin checks), and fix the prefix rather than the game.
- **Forcing a built-in DLL with an override can break what the native file made work (stage
  3.2b).** `xinput1_3=b` made x64 programs fail to load it at all, and Unreal's launcher refused to
  start. Putting Wine's file back in place (as `wineboot` does) worked. Lesson: prefer restoring the
  prefix's files over load-order overrides.
- **An opaque hint is not opacity on Lomiri (stage 3.2b).** A Wayland opaque region on the game's
  surface changed nothing; the buffer format did: our Halium path presented opaque BGRA pictures as
  Android's BGRA_8888, and Lomiri blended them with the game's leftover alpha. Lesson: when a picture
  is see-through, follow the buffer to the compositor and check its real format first; measure a fix
  before calling it done (patch 0011 was called a fix on the strength of one unverified look).
- **A GPU limit can hide behind a hang (stage 3.2b).** Stray in D3D12 hung before its first frame;
  the cause was one log line: VKD3D-Proton needs 5 descriptor sets, Turnip offered 4 on Adreno 6xx.
  Turnip's own option (`tu_dont_reserve_descriptor_set`) gave 5. Lesson: read the translation
  layer's errors before the hang's symptoms, and look for the driver's options before patching it.
- **An installer's "exit 0" is not a finished install (stage 3.2b).** Microsoft's Visual C++ 2022
  setup over an older Microsoft runtime returned 0 but left `vcruntime140.dll` missing (Wine's
  installer removed it again with the old product); a `/repair` restored it. Lesson: after an
  installer, check the files it should have left, and repair when they are missing.
- **Bound a possible gain before building it (stage 3.3).** Whether esync was worth porting came
  down to three cheap numbers: the server's requests per frame (`strace -c`), how much of the main
  thread's time is blocked at all (`schedstat`: 8 % in the CPU-bound game), and what one round trip
  costs (about 20 µs, a small benchmark). Together they capped the gain at about 2 %. Lesson: before
  a large port, measure the most it could give.
- **A vendor's tree can assume the vendor's build (stage 3.3).** Proton's Wine keeps no generated
  files in git (its `autogen.sh` makes them), and its 11.0 `makedep` builds no `bin/wine` when
  cross-compiling, because Valve builds natively on ARM64. Lesson: when adopting a tree, read its
  owner's build recipe first (Proton's `Makefile.in`), and expect gaps where our way of building
  differs from theirs.
- **Two quick runs are not a comparison (stage 3.3).** Proton's Wine ran the graphics tests up to
  twice as fast as ours, run by run; but the phone had warmed up between the runs, Lomiri's
  orientation (and so the picture sizes) differed, and the two bases take different picture paths.
  Lesson: interleave the runs, fix every outside condition (orientation, temperature, background
  load), and check that both sides do the same work before reading a gap.
- **A frame rate means nothing until the picture is seen on the screen (stage 3.3).** Proton's Wine
  reported twice our frame rate for a whole night of alternated, careful runs, and I chased the gap
  through three patches (two kept for other reasons, none closing it). Its window had lost its
  Wayland role before the first frame: the frames were thrown away, never shown. The screen was also
  off for every automated run (backlight 0), so no screenshot could have caught it, and screen-on
  numbers are lower (ours: 105 fps off, 75 fps on). Lesson: before comparing, confirm with the screen
  on (a screenshot, or the maintainer watching) that every side really shows its picture, and note
  the shell's orientation; read the Wayland request log of both sides before guessing at causes.
  It happened twice: after the window was fixed, Proton's "30-70 %" in display mode tests came from
  a 1x1 or unscaled buffer (its scaler never switched on). The buffer sizes handed to the compositor
  (`get_server_buffer_handle` in `WAYLAND_DEBUG`) show at once whether both sides draw the same thing.
- **Rule out the settings before blaming the base (stage 3.3).** Goat Simulator hung at its first
  level on our Wine and played on Proton's, but the two runs had different game settings (windowed
  960x540 and lowest graphics, set by the maintainer on Proton's run). The same settings files
  copied to our prefix made our base fail at the same point again, this time with UE3's "Pure
  virtual function being called" crash in its render thread. Lesson: when one base works and the
  other does not, repeat the failing side with the working side's exact settings before writing
  down which base a game needs. A hang on one run and a crash on the next, at the same point, both
  fit two threads racing.
- **Valve's code assumes X11 in places that matter on Wayland (stage 3.3).** Proton's scaler for
  display modes sized its target from the surface's minimum size, which is the window on X11 and
  1x1 on Wayland, and read every game image as BGRA, while our Mesa makes DXVK draw RGBA. The
  results looked like other problems (an empty screen, a frame rate twice too high). Lesson: when
  carrying code written for gamescope or X11 over to Lomiri, check every size and format it takes
  from the surface, and read Wine's own log line for it ("scaling from ... -> ...") before
  measuring.
- **A disproved claim must leave the patch too (stage 3.3).** Patch 0012's description and code
  comment still said it halved the GPU time per frame after step 4 had measured no change; it was
  found only while porting it. Lesson: when a measurement disproves a reason, fix the patch text,
  the comments and the notes in the same step, not only the notes.
- **`gh` can pick the wrong repository (stage 3.3).** With an `upstream` remote (ARMDeck) next to
  `origin`, `gh release create` went to `GabrielCRadu/ARMDeck` (it failed with 404, so nothing was
  created there). Lesson: give `gh` the repository every time (`--repo alpha-liu-01/UTDeck`), and
  a full commit hash for `--target` (a short one is refused with 422).
- **Many quick adb connections can crash the phone's adbd (stage 3.3).** `steam-push.sh` made
  about fifteen `adb push` and `adb shell` calls in a row; Ubuntu Touch's `adbd` died with SEGV
  three times during it, and its restarted copy could not attach to USB again ("timed out while
  waiting for FUNCTIONFS_BIND") until the cable was plugged in again. It looked like the phone
  sleeping, and a phone restart in between hid the pattern. Lesson: read the phone's own log
  (`journalctl`, `adbd.service: Main process exited`) before guessing; send several files as one
  archive with one push and one shell.
- **A notification that is accepted is not a notification that is shown (stage 3.8).** Lomiri's
  notification server answered every call with an id, and the maintainer saw nothing; only
  Lomiri's own log showed its notification QML failing (`TypeError ... of null`) at each one.
  Lesson: for anything meant for the eyes, ask the maintainer, and read the compositor's log at the
  same moment.
- **The compositor can hang and the numbers keep coming (stage 3.4).** Lomiri stopped drawing
  (its render threads waiting on locks) in the middle of an automated run, while still accepting
  new windows; every later test "ran" at up to twice its real rate, because nothing took its
  frames. The screen kept its backlight, so the backlight check did not catch it. Lesson: before
  each round, check that Lomiri itself is alive (its CPU time moves, it answers on D-Bus), not
  only that the screen is on.
- **Count what a setting reached, not what a tool touched (stage 3.5).** The CPU boost's summary
  said 6 threads, which looked like it had missed the game; in fact all 51 of ATRI's threads had
  it, inherited from the boosted `steam.exe` that started it (the kernel copies `uclamp_min` on
  clone), while Steam's `reaper` had adopted the game so that it no longer looked like a child of
  ours. Lesson: check the effect on the target itself (`sched_getattr` per thread), and know who
  ends up as a process's parent under Steam.
- **A half-busy GPU can be a CPU problem (stage 3.5).** Goat Simulator ran at 5.9 fps with the GPU
  only 45 % busy and clocked down to the 300s, which looked like the game or Wine. The thread
  sample showed the render thread on the little cores: the scheduler saves power by putting
  threads that wait a lot on slow cores, and a game's threads that wait for each other look like
  that. With `uclamp_min` on the game's threads it ran at 14.1 fps. Lesson: when neither the GPU
  nor one core is full, look at which cores the game's threads run on before blaming the game.
- **Find a game's process by more than a name in its command line (stage 3.6).** Freezing "the
  process whose command line names ys9.exe" hit Steam's launch helper, a shell that carries the
  game's path; and taking the first word of the game's own command line failed on a folder with
  spaces ("Ys IX Monstrum Nox"), so the pause froze nothing. Lesson: the program is everything up to
  ".exe", Linux programs that merely name one are not it, and the result is checked on the target
  (its state `T`, its frames at 0) before trusting the tool.
- **Mask every line that can hold account data, not only the ones you expect (stage 3.6).** A
  check of Steam's sign-in printed the response line with the account's number in it: the filter
  masked numbers of six digits or more in some lines and not in the one picked out afterwards.
  Nothing reached a file or a commit. Lesson: apply the mask as the very last step of any command
  that reads Steam's logs, after every selection, so a later `grep -o` cannot bring an unmasked
  field back.
- **Adding a game in Steam starts the download at once, with its Linux build if it has one
  (stage 3.9).** Aperture Desk Job was added and Steam began fetching its Linux program files before
  UTDeck's tool was forced for it. Forcing it mid-download (Steam stopped, `set-default.py --app`,
  Steam started) was enough: Steam swapped only the program-files depot and kept the shared data
  already downloaded. Lesson: for a game that may have a Linux build, force the tool right after
  adding it; stage 4's library should do this before the first download.
- **A helper that acts on "the game" must know when it is not running a game (stage 3.9).** The
  pause helper (3.6) froze every Windows program of the prefix while the phone was locked, and
  Steam's install script is one: it froze Aperture Desk Job's first set-up halfway, and the broken
  set-up then hung the next install. Lesson: setup steps (install scripts, prefix creation) are
  never paused; a feature for games is switched off explicitly for everything else.
- **"Failed to launch" through hidden Steam can be a dialog nobody sees (stage 3.9).** Steam stopped
  before the game at "ShowInterstitials", a notice drawn on its invisible Xvfb screen. Lesson: when
  a launch stalls, read the `GameAction` lines in `console_log.txt` for the last step, and look at
  the hidden screen (`xwin.py :50 --shot`) before suspecting the game.
- **Wine's debugger cannot look into an emulated game, and attaching can end it (stage 3.9).**
  `winedbg "bt all"` on a hung Aperture Desk Job listed only Wine's native processes and skipped the
  x86-64 game (run through FEX); the failed attach ended the game. Lesson: on this runtime, read a
  hang from outside (thread states and `wchan` in `/proc`, the sensor log, the kernel log) and
  collect everything wanted before trying the debugger.
- **Read the game's own log before the driver's (stage 3.9).** "Failed to initialize Vulkan" from
  Aperture Desk Job looked like a gap in Wine or Turnip; Source 2's console log (`-condebug`) named
  the exact missing capability in one line (subgroup operations in drawing shaders), and Mesa's
  device list showed it is missing in the Adreno 619's hardware. Lesson: most engines can write a
  log of their own (`-condebug` for Source, `Player.log` for Unity, `Launch.log` for Unreal 3); turn
  it on first.
- **Newer Steam clients keep game art in hashed folders (stage 4.1).** UTDeck's library showed no
  cover for Plants vs. Zombies: Replanted and Stray's logo instead of its cover: both looked only in
  `appcache/librarycache/<appid>/`, while this Steam keeps some pictures in
  `librarycache/<appid>/<hash>/`, and names the portrait cover `library_capsule.jpg` there (300x450,
  like `library_600x900.jpg`). Lesson: search the game's cache folder one level down, by kind
  (portrait, then wide header, then logo), and never take a logo as the cover while a cover exists.
- **Lomiri gives every window a card and closes an app left with no window of its own (stage
  4.1).** With the game's window tied to UTDeck (patch 0016), the app switcher showed two UTDeck
  cards, the library and the game. Hiding the library is the fix (the Dolphin port did the same),
  but qtmir 0.7.2 closes an app 1 s after its only process has lost all its windows
  (`Application::updateState`: `lostAllSurfaces`, `m_stopTimer`), and the game's window counts for
  the app's window list only, not for its processes. Lesson: read the shell's lifecycle code before
  hiding an app's last window. Here the game's processes join the app through `DESKTOP_FILE_HINT`,
  and the library comes back the moment the game's process ends, before Wine's leftovers close.
- **An app paused by Lomiri while its click is upgraded can no longer be closed (stage 4.1).**
  UTDeck 0.1.0 was in the background (Lomiri had paused it, state `T`) when 0.1.1 was installed.
  Swiping its card away did nothing visible, and every new start of UTDeck waited behind it ("is
  closing. Queuing start"): Lomiri wanted to wake it before closing it, but by then knew only the
  new version ("Invalid app ID: utdeck.alphaliu01_utdeck_0.1.0.2x"). `kill -CONT` let the old one
  close, and Lomiri started the queued 0.1.1 by itself. Lesson: stop a running app before upgrading
  its click; `install-click.sh` now stops the app's systemd unit first. (adb's shell has no session
  environment: `systemctl --user` needs `XDG_RUNTIME_DIR` and `DBUS_SESSION_BUS_ADDRESS` given by
  hand, and `lomiri-app-list` lists nothing even then.)
- **Lomiri can take fullscreen away right after granting it (stage 4.1).** Once a new window has
  drawn, Lomiri gives it the state its app's last window closed in (`windowReady` in `Stage.qml`).
  Celceta asked for fullscreen, got it (1080x2400), then lost it to the library's saved "normal"
  state (2400x1017 under the top bar); the game kept a fullscreen-sized window and nothing asked
  again. It looked fixed in M1 only because a trip through the app switcher made the game create
  its window anew. Lesson: when a window state seems random, trace the configures (Wine's
  `trace+waylanddrv`): one run showed the grant and the take-back in order. A client that wants
  fullscreen on Lomiri must ask again whenever the compositor's state drops it (patch 0017).
- **Report the size the compositor gave, even in the normal state (stage 4.1).** Held in the
  normal state at 2400x1017 by Lomiri, Wine reported its whole 2400x1080 Win32 window as the window
  geometry. qtmir then believed the window was already 2400x1080 and skipped the resize to that size
  once fullscreen was granted (`MirSurface::resize` compares with the size it knows): the game sat
  63 px short under a hidden top bar. Patch 0018 reports the visible part clipped to the configured
  size, and Lomiri then sends the full size. Lesson: a client whose reported size disagrees with
  the compositor's can make the compositor's own bookkeeping skip later changes; when a size
  "never arrives", compare what was configured with what was reported, step by step.
- **`pgrep -f` in an adb shell matches the shell itself (stage 4.2).** A wait loop ran
  `adb shell 'pgrep -f "steam-install.sh 1"'` until the script ended; the pattern is also in the
  command line of the shell adb starts for it, so pgrep always found "a match" and the loop waited
  on for twelve minutes after the one-minute download had finished. Lesson: wait on something the
  job leaves behind (a pid file, the last line of its log, a done marker), or use a pattern that
  cannot match itself (`pgrep -f "[s]team-install.sh"`).
- **A 24.04-1.x phone has no apt package lists (stage 4.2).** `apt-get download xvfb` worked on stone
  only because its lists had been filled earlier; on the Nothing Phone (1) `/var/lib/apt/lists` is
  empty, and filling it writes to the system. Lesson: a setup that needs a package from Ubuntu fetches
  the `.deb` from the archive by its path and checks its SHA256 (as for Steam's files), and never
  relies on the phone's apt state.
- **Steam takes about a minute to sign itself in on a phone (stage 4.2).** After a start, the
  Nothing Phone (1) spent some 50 s "Connecting" before Steam reached Valve, and was signed in after
  59-66 s; the app checked once at 30 s and kept offering a needless "Sign in". Lesson: when waiting
  for another program's state, watch it (here every 5 s, up to 3 minutes, and look for the sign-in
  window to tell "still starting" from "needs a sign-in") instead of checking once after a guess.
- **A setup is only checked when the first game runs (stage 4.2).** Every setup check passed on the
  Nothing Phone (1), and the first game still ran through Steam's own Proton: UTDeck's tool was never
  made Steam's default there. On stone that had been done by hand in stage 3.S and forgotten as a
  step; a fresh device showed it at once. Lesson: a setup's exit run includes starting one real
  thing through it (here a game), and every hand step done on the development phone is listed
  when its stage ends.
- **Closing "every window" also closes the question (stage 4.3).** `steam-service.sh play` ended
  with `xwin.py --close` on every mapped window, meant for Steam's main window; it also closed a
  dialog Steam was waiting on, which dismissed it without an answer, and the launch waited on with
  nothing on screen. Lesson: a clean-up acts on what it means (here windows the size of the screen),
  never on "everything".
- **A step that only runs on creation misses what someone else created (stage 4.3).** Our tool put
  DXVK into a prefix only when it made the prefix; Steam's own Proton had made Ys IX's first, so the
  game had no Direct3D. Lesson: check for the result (the right DLLs, from this runtime), not for
  having done the action.
- **Wait on what adb prints, cleaned, never on its exit code (stage 4.3).** Two wait loops over
  `adb shell` ended at once: the phone's adb shell always exits 0 (`runtime-push.sh` already said so),
  and its output carries a carriage return, so `[ "$s" = active ]` never matched. A second test then
  started on top of the first. Lesson: compare adb's output after `tr -d '\r\n '`, and test a wait
  loop once on something known to be running.
- **Check the phone's free space before writing to it (stage 4.3).** stone's disk was full (19 MB
  of 101 GB); `runtime-push.sh` failed halfway, leaving a half-unpacked runtime, and an `adb push`
  of `steam-run.sh` replaced the file with an empty one, so Steam did not start. Lesson: a script
  that writes much to the phone checks the free space first and stops before changing anything
  (`runtime-push.sh` now needs 1,600 MB), and older versions kept "for going back" are counted and
  cleared from time to time, with the maintainer's OK.
- **Not every Windows program Steam starts is the game (stage 4.3).** Steam runs its own graphics
  check (`d3ddriverquery64.exe`, app id 0) through the compatibility tool after signing in. UTDeck
  took its end for the game's end and, with Steam's switch off, stopped Steam in the middle of the
  real game's install script. Lesson: classify every run of the compatibility tool (the game, its
  install scripts, Steam's own tools) and let only the game drive "the game has ended".
- **A failed game does not always end (stage 4.3).** Without Vulkan, Ys: Memories of Celceta's game
  spun at a full core after DXVK's error instead of quitting, and its launcher window had already
  counted as "the game's window in front". A check that waits for the game to end never fires then.
  Lesson: watch the running game's own log for fatal errors, and offer to end the game.
- **Not every Steam dialog is a window of its own (stage 4.3).** The "Controller using Steam Input"
  notice is a separate window, but a publisher's agreement (ShowEula) is a box inside Steam's main
  window, which UTDeck keeps closed: the "Steam is asking" page showed a black rectangle. And the
  hidden screen, sized like the phone (1200x540), cut the agreement's buttons off. Lesson: when
  Steam waits for an answer, look for where the question is really drawn (open the main window if
  nothing is mapped), and give the hidden screen the size Steam's own windows expect (1280x800).
- **Steam can save an answer without passing it on (stage 4.3).** A publisher's agreement accepted in
  Steam's main window was saved for the account (`localconfig.vdf`), but the launch that waited for
  it stayed waiting for good, and Steam's log said nothing. Lesson: do not rely on one signal for
  "the user answered"; check where the answer is stored, and have a way out (Cancel) of every wait
  the user is put in.
- **A vendor kernel's version says little about its drivers (stage 4.4 planning).** Planning the
  controller stage, I said the phones' Linux 5.4 kernels were too old for the DualSense
  (`hid-playstation`, upstream 5.12) and Switch Pro (`hid-nintendo`, 5.16) drivers. The Dolphin
  port's stage 0 had already read stone's `/proc/config.gz`: both drivers are there (backported).
  Lesson: read the kernel's own configuration (and earlier notes) before reasoning from its
  version number.
- **Wine drops some controllers on purpose (stage 4.4).** For PlayStation and Nintendo pads, Wine's
  `winebus` ignores the controller on its normal paths and takes it only through the raw HID node
  (`/dev/hidraw*`); on stone that node is closed to phablet, so the T6 in Switch mode reached no
  Windows program at all, with only a `warn:hid` line to say so. Lesson: when a controller "does
  nothing" in Wine, read `winebus`'s own decisions (`WINEDEBUG=+hid`: "ignoring ... device",
  "Permission denied") before suspecting the controller.
- **A controller database entry can fit the ID but not the driver (stage 4.4).** SDL's built-in layout
  for the Switch Pro Controller matched the T6 by its ID, but was written for another version of the
  kernel driver (D-pad as a hat, no Capture button), so every button after Capture was one off and the
  D-pad was missing. Lesson: compare the kernel's own events (`evtest.py`) with what the game layer
  reports before trusting a mapping; and a stick that reports a square, not a circle, can look like a
  stuck axis when it is moved along an edge.
- **adb's shell has no user session (stage 4.4).** `systemd-run --user` from `adb shell` failed with
  "Failed to connect to bus: No medium found": adb's shell has neither `XDG_RUNTIME_DIR` nor
  `DBUS_SESSION_BUS_ADDRESS`. Set both (`/run/user/<uid>`, `unix:path=/run/user/<uid>/bus`) first.
  And a test that needs the only USB port (a USB controller) runs as a user service that writes a
  log, collected when adb is back.
- **Do not install the app while a game runs (stage 4.4).** I installed a new UTDeck while Celceta
  was still running (its log had no "exit" line yet); installing stops the app, the game went on
  without it, and its library would not have come back at the game's end. Lesson: check the game
  file first; `install-click.sh` now refuses while UTDeck's game file names a running game.
- **A memory guard must watch the right process (stage 4.5).** A diagnostic run meant to stop
  wiliwili at 1.8 GB found its process with `pgrep -f 'wiliwili.exe$'`, which matched a 1 MB Wine
  helper whose command line also ends in the program's name. The game itself grew unwatched to 5.7 GB
  until the kernel's out-of-memory killer ended it (stone survived; its swap was nearly full). Lesson:
  check the size the guard reads before trusting it, and stop at the first reading that makes no
  sense. The second attempt failed too: it picked the process by its Windows path in the command
  line, tested against a made-up process line; on this Wine the game's command line starts with the
  loader's path and only its process name is `wiliwili.exe`, so the guard saw nothing and the game
  reached 4.6 GB. So: test a selector against the real process, and put the guard on what cannot be
  missed, the phone's free memory (stop the prefix's Wine below 1.5 GB available).
- **A program stuck at 100 % CPU in FEX may be doing x87 maths (stage 4.5).** wiliwili drew nothing and
  ate memory; its main thread was never in a system call, so it had to be frozen (`SIGSTOP`) to read its
  stack, which was all FEX's software 80-bit maths (`cephes_f128_sinl`, `softfloat_*F128`).
  `FEX_X87REDUCEDPRECISION=1` made it start in a second. Lesson: when an x86 program spins under FEX,
  look up the stack's FEX offsets in the unstripped build before suspecting the program.
- **What a path learned once must reach every new path (stage 4.5).** Stage 2.4 found that Box64
  stalls on self-unpacking installers and that FEX runs them, and Steam's install scripts got FEX for
  it; the new "add a game from its installer" path started 3DMark's installer with the default
  (Box64) and it spun for 6 minutes with no window. Lesson: a new way of running something reuses the
  settings of the closest existing way (here `utdeck-compat`'s for install scripts), and the notes are
  searched for the program's name before its first run.
- **Two automatic actions on the same thing race (stage 4.6).** "Stop Steam when a game ends" (with
  Steam's switch off) and "start Steam for the game just tapped" ran within a second of each other;
  Steam was shut down under the new launch. Lesson: an automatic stop waits a little and is called off
  by the next start, rather than acting at once.
- **A process joined to an app must be able to close (stage 4.6).** Steam's windowless install
  scripts carried UTDeck's app id, so Lomiri waited for them when the user swiped the app away, and
  killed it minutes later. Only processes that can show a window and answer a close request are joined
  to the app.
- **Steam's game manifests hold the account's number (stage 4.7).** A `grep -A3` over an
  `appmanifest_<appid>.acf` printed its `LastOwner` line, the account's 17-digit Steam ID, into the
  session's output (not into any file or note). Lesson: every read of a Steam file goes through the
  number mask (`sed -E 's/[0-9]{9,}/(n)/g'`) before it is printed, `.acf` files included, not only the
  logs.
- **A game's own folder can hold files named after the account (stage 4.7).** A `find` over Aperture
  Desk Job's leftover install folder (`common/`, its settings) listed `user_<number>_slot0.vcfg`, the
  account's number as a file name, unmasked into the session's output. Lesson: the number mask applies
  to file listings in Steam's folders too, not only to file contents.
- **A click packaged without building carries the old app (stage 4.7).** `build-click.sh` only packs
  `build/app/utdeck`; run without `build.sh` first, it made "0.3.30" with 0.3.29's app inside, and the
  new Downloads button was missing on the phone. Fixed: `build-click.sh` now refuses when an app source
  is newer than the build.
- **Nothing kept the phone awake during a game or a download (stage 4.7).** With the screen's timeout
  on, a game played with a controller (no touch on the screen) had its screen turned off and the phone
  locked mid-game, and Steam's download stopped once the phone slept. Only two test scripts held the
  screen on (`ab-env.sh`, `ab-gfx.sh`); the app never did. Fixed with `utdeck-awake.py` (repowerd's
  `keepDisplayOn` while the game is in front, `requestSysState` while Steam downloads). Lesson: what a
  test script works around (here: the screen going off during a measurement) is a hint that users hit it
  too.
- **A watcher that reads one sign of activity misses the cases where that sign stays still (stage
  4.7).** The download keep-awake decided "Steam is downloading" from the growth of
  `steamapps/downloading/<appid>`; Barro 2020's download left that folder empty until its last second,
  so a longer download of that kind would have lost its keep-awake after 3 minutes. Now Steam's own
  content log counts too. Lesson: a helper that acts without being seen writes down what it decided
  (`steam/awake.log`), so a failed test can be read afterwards instead of guessed.
- **Steam Cloud never synced a game on UTDeck, and nothing said so (stage 4.7).** Four games started from
  a fresh save; Steam's cloud log had thousands of "Unable to resolve path with root WinSavedGames"
  lines. Steam puts a Windows game's cloud saves in the prefix's `users/steamuser` (Proton's user name);
  UTDeck's Wine names the user after the Linux user (`phablet`). Lesson: a compatibility tool must give
  Steam the same layout Proton does, and Steam's own logs (`cloud_log.txt`, not only `console_log.txt`)
  are read when a game behaves differently than on the PC.
- **Wine's defaults put game files into the phone's own folders (stage 4.7).** A new Wine prefix links
  the Windows user's Documents, Downloads, Music, Pictures and Videos to the Linux user's, so Goat
  Simulator's settings and crash dumps and 3DMark 11's shader cache sat in the phone's Documents, shared
  by every game and outside Steam Cloud's reach. Proton makes them real folders; UTDeck does too now
  (`utdeck-profile.py`). Lesson: compare a prefix UTDeck makes with one Proton makes, folder by folder,
  not only the DLLs.
- **`pgrep -f NAME` over adb matches its own command line (stage 4.7).** A safety check "is Celceta
  running?" with `pgrep -f celceta` inside `adb shell '...'` found the shell running that very command,
  and stopped the step. Lesson: check a game through UTDeck's own state file
  (`$XDG_RUNTIME_DIR/utdeck/game`) or a pattern that cannot match itself (`[c]elceta`).
- **Keep-awake started with the game, not with the tap (stage 4.7).** The screen request began when
  `game-run.sh` started the game, so the minute or more before it (Steam's start, its dialogs,
  first-start install scripts, the launcher) ran under the screen's timeout and the screen went off
  during loading. Fixed by holding it from the starting page. Lesson: a feature about "while the user
  waits" covers the whole wait as the user sees it, from the tap, not the part one script sees.
- **An app started from adb is not the app the user starts (stage 4.7).** After each install the app was
  started with `adb shell lomiri-app-launch ...`; the adb shell has no `XDG_RUNTIME_DIR`, the app got
  `~/.cache` instead, watched the wrong folder for game-run.sh's state and never saw a game run: the
  starting page stayed and then said "The game ended before it started". It looked like a bug of the
  change just made (the starting page's keep-awake). Lessons: the app no longer trusts that variable;
  and when a test fails right after a change, compare the environment of the failing run with a working
  one before blaming the change.
- **A marker per game where the thing it marks is per prefix (stage 4.7).** "Prefix prepared for runtime
  X" was kept once per game; since 4.6 a game has a prefix per Wine base, so switching base and back made
  the next start update the whole prefix again (49 s). Lesson: when one thing becomes several (one prefix
  per base), check every marker and cache that described the one.
- **Two cases out of 132 are not a rule (stage 4.7).** "Every start shows a 10 s update check" came from
  the only two launches that had the stage; the maintainer's next two launches had none. Lesson: count
  how often a stage occurs before describing it as what always happens.
- **"The log is still being written" is not "an upload is running" (stage 4.7).** Step 9's stop waited
  until Steam's cloud log had been quiet for 10 s; right after Steam's start the log is busy for a
  minute with every game's cloud check, so a stop then took 99 s. Lesson: wait on what a log says (the
  last upload line), not on its time stamp; the time stamp alone also counts unrelated activity.
- **Steam's shader pre-caching on a phone compiles for the CPU (stage 4.7).** With no system Vulkan
  driver, Steam's `fossilize_replay` used SwiftShader: work our Turnip games can never use, plus
  re-encoded videos no runtime here reads (760 MB for one game). Lesson: before keeping a desktop
  feature, check which device it actually targets on the phone (`shader_log.txt` names it).
- **A folder Steam leaves is not a job Steam runs (stage 4.7).** After a file check that found nothing,
  Steam left an empty `steamapps/downloading/<appid>`; UTDeck counted every folder there as a download,
  showed a "repair" stuck at 0 MB, and would have kept Steam running and the phone awake. Lesson: read the
  state Steam keeps for the game (`StateFlags`), not only whether its folders exist; and a command Steam
  may refuse ("VerifyApp ... Completed with "0"") needs its answer read, not only its success awaited.
- **A fix for one path of the same action (stage 4.8).** In 4.3 the prefix update (`wineboot -u`) got
  "no window" (display drivers off); making a new prefix (`wineboot -i`) did not, so a game's first start
  still showed Wine's setup window. And the starting page timed out because Steam calls a game started
  before UTDeck's tool has prepared Wine. Lessons: when fixing one call of a tool, search for its other
  calls; and a step that can take minutes tells the app it is running.
- **A control that already handles a key, pressed a second time (stage 4.8).** Main.qml pressed the
  focused control on Return (`trigger()`), but Lomiri's buttons, switches and list rows press themselves
  when Return is released (`UCAbstractButton` and `UCListItem`'s `keyReleaseEvent`). A button only
  emitted an extra, unused signal; a switch toggled twice and never moved. Lesson: before adding a key
  handler above a toolkit's controls, check what the controls already do with that key (press and
  release).
- **Input that goes around the compositor does not keep the phone awake (stage 4.8).** The controller
  is read through SDL from the kernel's devices, not through Lomiri, so repowerd (the phone's power
  service) saw no activity and turned the screen off while the app was used with a controller. repowerd
  has no "the user is active" call; a short screen hold after each use does the same, since ending a
  hold restarts repowerd's full timeout (checked on stone: off 61 s after the release with a 60 s
  setting).
- **On a phone toolkit, focus on a text field means the keyboard (stage 4.8).** Lomiri's text fields open
  the on-screen keyboard whenever their input gets the focus (`InputHandler.qml`), which is right for a
  tap and wrong for a D-pad passing through a settings page. Found by reading Lomiri's code before the
  test, not after. Lesson: for each kind of control a new input method reaches, read what the control
  does on focus, not only on press; and a model reset (the library's refresh) also resets a view's
  selection, so a selection meant to last has to be found again by its id.
- **Memory a program leaves behind outside its own processes (stage 4.9).** Looking for UTDeck's files
  outside the home folder for the uninstall, `/dev/shm` (shared memory, kept in RAM) held 328 MB of
  Steam's, 272 MB of it used by no running process: what earlier Steam runs left. Measuring a program's
  memory by its processes (PSS, stage 3.4) never sees this. Lesson: when counting what a program costs
  in memory, also look at the RAM-backed folders it writes to (`/dev/shm`, `/tmp` on tmpfs), and check
  what is left there after it exits.
- **A path helper that assumed every path was under home (stage 4.9).** The uninstall's list showed
  `/tmp/steam` as `~/tmp/steam` (it cut the home folder off and put `~` in front, for any path). Caught
  on the phone's first plan, since the test home had no `/tmp` items. Lesson: a test home tests what is
  inside it; whatever a script touches outside it needs a look on the real phone (read-only, as here).
- **A progress count that included what was already there (stage 4.9).** The setup showed Steam's
  download as the size of Steam's whole folder, every 3 s. After an uninstall that kept the games, that
  folder already held 18 GB of them, so the count started at 18,000 MB (and `du` walked 18 GB of files
  each time). Now the kept folders (`steamapps`, `userdata`) are left out. Lesson: a progress number
  measured from a folder's size counts only what the step writes, so whatever an earlier install left
  in that folder is excluded.
- **"Signed in" is not "the sign-in is saved" (stage 4.9).** The setup took Steam's "Logged On" (its
  connection log) as the sign-in being done, and its next step stopped Steam 2 s later to set the
  default tool. Steam saves the sign-in (`config/loginusers.vdf`) only when its login reaches "Success"
  (`steamui_login.txt`), a few seconds after "Logged On": stopped before that, it started again signed
  out and the QR code had to be scanned twice (the Nothing Phone (1), the first setup after the
  uninstall). Now the step also waits for "Success". Lesson: before stopping a program
  right after it reports a state change, check when it writes that state to disk, not only when it logs
  it.
- **Steam Input on a phone that has `/dev/uinput`, with Steam hidden (after stage 4.9).** On the
  Nothing Phone (1) a DualShock 4 clone worked in Plants vs. Zombies but not in Ys IX. Ys IX opts in to
  Steam Input ("Opted-in Controller Mask ... fffd" in Steam's `controller.txt`), so Steam hides the real
  pad from the game (the game's SDL saw only "Steam Virtual Gamepad", `28de`) and feeds the virtual one
  from its active layout. Steam activated only its desktop layout (app 413080) and never the game's: it
  tells which game is in front from its own X display, and UTDeck's Steam runs hidden on Xvfb while the
  game's window is on Lomiri. So the pad drove Steam's invisible desktop and the game got nothing.
  Stone never showed it: without `/dev/uinput` Steam cannot make virtual pads, so games always read the
  real pad (stage 4.4's `controllers.reg`). Tried first: Steam's own per-game "Disable Steam Input"
  (`UseSteamControllerConfig` "0" in `localconfig.vdf`, the value read from Steam's UI code). Steam kept
  the value but still applied the game's opt-in, so the place or the way Steam reads it is something
  else; removed again. Solved in `utdeck-compat`, which already passed the real pads on stone (stage
  3.2b): whenever Steam tells a game to ignore the real pads, the game gets them anyway and Steam's
  virtual pad (`28de:11ff`) is hidden from it instead. Ys IX then saw the "Wireless Controller" and
  responded (the maintainer, 2026-11-23). Lessons: a feature tested on one phone can take another path
  on a phone with one more device node, so list the nodes a program uses (here `uinput`, `hidraw`) on
  each test phone; and fix a cause at the point all its cases pass through (the game's launch), rather
  than in a setting the program may read from elsewhere.
- **A picture list that looked only where pictures are not (stage 4.5, fixed after 4.9).** An own game's
  cover could only be one of the pictures UTDeck found itself: in the game's folder and at the top of
  `~/Pictures`. On Ubuntu Touch, `~/Pictures` keeps its pictures in folders (`Screenshots`, the
  camera's `camera.ubports`), so nothing of the user's showed, and there was no way to pick one
  elsewhere. Now the list also looks one folder down, and "Choose a picture..." asks Gallery or File
  Manager for one through Content Hub (Ubuntu Touch's service for passing files between apps); the
  picture is copied into the game's folder, since Content Hub leaves it in the app's cache. Lesson: on
  a phone, choosing a file goes through the system's picker, not only a list the app builds; and check
  where the system's own apps actually save (here, one level down).
- **A sampler written for one phone's CPU layout (found 2026-11-24, fixed in 5.1 step 2).** `game-run.sh`
  reads the big cores' clock limit from `cpufreq/policy6`: stone's layout (6 small and 2 big cores).
  The Nothing Phone (1) has `policy0`, `policy4` and `policy7`, so the read failed, the shell's
  arithmetic on the empty value ended the whole sampler, and every `.sensors` file there (19 runs) is
  empty; nobody noticed, since only stone's were read. Lesson: device paths are found on the phone
  (by listing, by name), never written in for one phone; and one failed reading must not end the
  others (each read with a fallback). Fixed 2026-11-24: one awk process finds the clusters
  (`policy*`) and the zones (by name) at the start and reads each file with a fallback (`?` for a
  missing or unreadable one, such as the Nothing Phone (1)'s `policy0/scaling_max_freq`); per-cluster
  load and clocks, the board sensor of that phone's thermal profiles, the battery's current; 3 small
  processes per sample instead of about 25. Checked on the Nothing Phone (1) and on stone, in PvZ.
- **An overlay that costs more than it should (stage 5.1, found 2026-11-24, open).** MangoHud as it is
  measured no cost in PvZ (held at 28 fps by a full GPU), but on wiliwili's home page (about 97 fps) it
  took 16-17 % of the frame rate at the full level and 11 % with only the frame rate shown (runs on,
  off, on, so the phone's heat is cancelled out). The first suspect was MangoHud drawing over the whole screen
  image every frame (on a tiler GPU like Adreno, reading and writing the full picture even for one line
  of text); a patch limiting the drawing to the overlay's rectangle changed nothing measurable, and
  with the overlay hidden (nothing drawn) the frame rate was the same as with one line shown. So the
  cost is not the drawing; on this screen no part of the phone is full, and the CPU governor's choice
  of clocks may be what moves. Open: runs off, hidden, off with the clocks recorded. Lessons: test the
  suspected cause in isolation (here: hidden, loaded) before building a fix for it; a
  cost is measured where it can show (a light scene at a high frame rate, not one a full GPU holds
  back), with the runs ordered so heat cannot explain the result; and a tool made on desktop GPUs is
  checked against how a phone GPU works before it is called cheap.
- **The screen flashing between the game and the wallpaper (stage 5.1, 2026-11-24, parked).** During the
  overlay's cost runs on the Nothing Phone (1), the screen began to flash between wiliwili and Lomiri's
  wallpaper: every few minutes at first, then a few times a second after the overlay was hidden through
  its control socket. The game's own frame log showed nothing (no long frames, about 86 fps), so the
  game kept drawing and Lomiri lost its picture. Lomiri then stayed broken (UTDeck no longer started
  correctly) until the phone was restarted. The maintainer had never seen it before and saw it start
  only with run 4, the first with the patched MangoHud (the redraw limited to the overlay's
  rectangle). Run 5 (the original MangoHud, from a fresh boot) ended
  after a minute with Lomiri itself crashing (`Fatal glibc error: pthread_mutex_lock ... assertion
  failed`, killed with ABRT, restarted by systemd; the game ended with it), so the patch was not the
  cause. MangoHud's only direct contact with Lomiri is a `wl_seat` and `wl_keyboard` it binds on the
  game's own Wayland connection for its keyboard shortcuts, which UTDeck does not use; a MangoHud
  patch left that out, and run 6 (5 minutes) looked clean, but the flashing came back later in that
  run and grew in run 7, **which had no MangoHud loaded at all**, so MangoHud is not the cause either
  (both patches are kept out of the tree). After the next restart, a 16-minute run without MangoHud,
  with Lomiri sampled every 15 s (read only): at 14:40:46, 1:40 after the game started, **the whole
  screen froze** (Lomiri's clock stopped), with no flashing before it. The game went on drawing, but
  every frame then took exactly 100 ms (10 fps): Lomiri no longer asked it for frames. Lomiri's
  memory, open files, GPU buffers and GPU memory stayed flat for the whole 16 minutes (no leak); its
  three render threads waited (no CPU), and so did the system compositor's drawing thread
  (`lomiri-system-compositor`, `Mir/Comp`, which hands Lomiri's picture to the display hardware). The
  kernel logged nothing. So the display chain below the game stops (sometimes briefly: the flashes;
  sometimes for good: the freeze, or Lomiri's crash). Still open: what triggers it. Not MangoHud; not
  a leak; `adb` commands (sent often during these runs) do not register login sessions. Remaining
  suspects: wiliwili presenting about 100 frames per second (`MESA_VK_WSI_PRESENT_MODE=mailbox`,
  UTDeck's default) to a 60 Hz screen for many minutes, which no game did before; and `adb` traffic
  itself. A run with no `adb` command at all (15:02-15:20,
  no MangoHud) flashed again after about 17 minutes, so `adb` is ruled out. The game kept 100 fps
  without a stall; while it flashed, the system compositor's drawing thread (`Mir/Comp`) used 8.8 % of
  a core and Lomiri's Wayland thread (`Mir/Wayland`, which receives the game's frames and logs one
  `wl_subsurface.visiblity not implemented` warning per frame) 8.2 %, with the phone warm (CPU 58 °C,
  skin 44.8 °C). (These were first reported 10 times too high, 88 % and 82 %: the per-thread CPU
  counts are hundredths of a second, and 5 s at full load are 500 of them, not 50. The wrong figures
  led to the theory that the display chain was overloaded by 100 frames per second; the right ones
  do not support it.) `FIFO` (paced by the screen) deadlocks under Wine on Lomiri (stage 2), so the
  test was a cap: `DXVK_FRAME_RATE=60` (run 9). The cap held
  (exactly 60 fps, 60 warnings a second) and **the screen flashed anyway**, after about 19 minutes:
  the frame rate is not the trigger. While it flashed, Lomiri's three render threads (`QSGRenderThread`,
  which draw the screen) each used 9-9.4 % of a core, the system compositor's `Mir/Comp` 8.8 %,
  `Mir/Wayland` 4.6 % (corrected as above): far from full; all on the big cores (4, 6, 7) with the game's boosted threads; the phone was
  limiting its speeds a little for heat (big cores 2.05 of 2.4 GHz, the fastest 2.21 of 2.52 GHz, the
  GPU 550 of 608 MHz). So the compositors are not short of CPU time when it flashes. Lomiri then
  crashed a second time (15:4x). Next: stone (Adreno 619, app 0.3.48, runtime `11.16-20261122`, Lomiri
  up for 2 days) ran wiliwili the same way, to see whether the problem follows our software or stays
  with the Nothing Phone (1): **27 minutes clean** (15:45-16:12, about 56 fps, never under 40, no
  flash, no freeze; Lomiri's render thread 75 % of a core, `Mir/Wayland` 30 %, `Mir/Comp` 12 %, the
  same at the start and the end; skin 33.6 °C). Differences left between the two: the phone (GPU,
  display, its Lomiri build), its heat (skin 42-45 °C on the Nothing Phone (1)), and our versions
  (stone: app 0.3.48, runtime `11.16-20261122`; the Nothing Phone (1): app 0.3.51, runtime
  `11.16-20261124`, in use there since 2026-11-23 22:56). Same system on both (Ubuntu 24.04.5,
  Lomiri 0.5.0, Mir 1.8.3, the same `lomiri-system-compositor`). Run 10 put the Nothing Phone (1) back
  on runtime `11.16-20261122` (no patch 0020, so wiliwili was portrait in the left 45 % of the screen
  again): **it flashed again**, about 20 minutes in (16:39). So the cause is not in our versions and
  not the landscape picture. The kernel logged no display event after boot (only an unrelated timeout
  on the unused modem's data link, `rmnet_ipa0`, 3 minutes before); the phone was draining its battery
  while plugged into the PC (about -500 mA). Left: something on the Nothing Phone (1) itself after about
  17-25 minutes of a game on screen; whether it needs a game at all is not known yet. Then PvZ
  (a DXVK game through Steam, fullscreen landscape, about 27 fps) ran 26 minutes on the same phone and
  runtime (16:43-17:09): **no flash**. So on the Nothing Phone (1) it comes with wiliwili (a GLFW
  program drawing through DXVK at 60-100 fps), not with every game; stone runs wiliwili cleanly. Why
  is not known (a guess: something in how wiliwili's drawing meets Turnip on the Adreno 642L, or
  Lomiri on that phone). **Parked** (the maintainer, 2026-11-24): wiliwili is not used for long runs on
  the Nothing Phone (1), and is watched when it is used. Lessons: change one thing per run, and test
  the other program early (here PvZ ruled wiliwili in after nine runs that only ruled things out); and
  a phone being tested also needs a charger that keeps up (the PC's USB port did not).
  Lesson so far: when a symptom
  appears during a test, run the same test without the newly added part early (here a run without
  MangoHud), before building fixes around the first suspect; measure the processes that show the
  symptom (here the compositors), not only the game; and check a new formula on a case with a known
  answer before building a theory on its numbers.
- **A vendor setting that only applies at the next start (stage 5, 2026-11-24).** The Nothing Phone (1)
  throttled Stray from 30 to about 16 fps with Nothing's most cautious thermal profile (`mid`): its
  Android picks a profile through its system server, which a Halium port does not run, so
  thermal-engine falls back to `mid`. Setting `persist.vendor.thermal.config` to Nothing's
  `high.perf` profile made init restart thermal-engine, which failed silently: init stops a service
  through its process group, and on this port it could not create those groups at boot. The old
  daemon kept the old profile; the property is read at the next boot. Lessons: on a Halium port,
  check that a service really restarted (its start time), not only that init was asked to; and look
  for the vendor's own choices (here nine profiles) before thinking of going past the vendor's values.
- **A sensor named by a guess (stage 5.1, 2026-11-24).** Step 1 said MangoHud's CPU temperature came
  from "the small cores' cluster" (`cpuss-0-usr`), and a patch was built to show the hottest of the 14
  CPU zones instead. Read side by side, `cpuss-0-usr` was on average 0.3 °C below the hottest zone
  (at most 4 °C for a moment), and sometimes the hottest itself: a CPU subsystem sensor that follows
  the hot spot. The patch was taken out again. Lesson: read a sensor next to its neighbours at the
  same moment before deciding what it measures; a name like `cpuss-0` says less than it seems.
- **An offset that points the other way on the right (stage 5.1, 2026-11-24).** The overlay's
  settings moved it 100 pixels in from the side to clear the rounded screen corners; on the left it
  worked, on the right it went further into the corner. MangoHud adds `offset_x` to the position in
  every corner (on the right: the screen's width minus the panel's width plus the offset), so the
  right side needs a negative one. Lesson: read how a program uses a setting in each case (here each
  corner) before choosing one value for all of them; and check each case on the screen.
- **A game installed from Steam Mobile got its Linux build (stage 5.1, 2026-11-24, left as it is).** Stardew
  Valley, bought that day, was installed through the Steam Mobile app's "install on this device":
  the phone's hidden Steam downloaded it directly, without UTDeck's install step (`steam-game.sh`),
  which maps a game that also has a Linux build to UTDeck's compatibility tool first. Steam then took
  the Linux build (depot 413153: `libcoreclr.so`, `libSDL2-2.0.so.0`), an x86-64 Linux program UTDeck
  cannot run. Fixed for this game by running UTDeck's install step afterwards (the mapping, Steam
  restarted, the install asked again): Steam fetched the Windows build (depot 413151, 287 chunks,
  the shared content reused); a few Linux files stay behind, unused. Not fixed (the maintainer,
  2026-11-24): support for Linux games is to be built before UTDeck ships as a product, and with it a
  game's Linux build becomes usable (Stardew's Linux build, for one, can draw on Wayland through
  SDL2), so the check that would force the Windows build is not added. Lesson: a game can arrive on the phone by more ways than UTDeck's own
  buttons (Steam Mobile, the store website); what UTDeck needs of an installed game is checked where
  the games are listed, not only where UTDeck installs them.
- **The Nothing Phone (1) dropping off USB during tests (stage 5, 2026-11-24, open).** Four times in
  one evening `adb devices` listed nothing while a game ran (Stray, wiliwili, Stardew Valley), each
  time cured by replugging the cable; the phone and the game kept running. The same tests showed the
  phone drawing more than the PC's USB port gives (the battery falling while plugged in, its charging
  also limited by heat). Not known whether the port's power is the cause. Worked around: test scripts
  run on the phone detached from `adb` (`setsid nohup`), and the PC's watchers retry, so a drop no
  longer cuts a measurement short. To try if it goes on: a powered USB hub, or `adb` over Wi-Fi with
  the phone on a charger. Lesson: a long measurement should not depend on the cable staying up; run
  it on the phone and only collect its results over `adb`.
- **A 32-bit game out of address space on our Wine base (stage 4.10, 2026-11-25).** Celeste froze
  seconds into play on our base and ran at a stable 60 fps on Proton's. Its log: Wine's "out of memory
  for allocation" and Wine Mono's assertion in `mono-mmap-windows.c`, with gigabytes of RAM free: a
  32-bit program without the "large address aware" flag gets 2 GB of address space, and Proton's Wine
  gives every 32-bit program 4 GB by default (`WINE_LARGE_ADDRESS_AWARE`), Hangover's does not. Fixed
  with Wine patch 0021 (the same switch and default). Lesson: "out of memory" in a 32-bit process is
  usually its address space, not the phone's RAM; and when a game works on Proton's base and not on
  ours, Proton's defaults (its `proton` script and its Wine's environment switches) are the first
  place to look.
- **Celeste: Box64 and Mono's thread states (stage 4.10, 2026-11-25).** With patch 0021 the address
  space was no longer the problem, and Celeste still stopped: Mono's check of its threads' states
  failed (`Cannot transition thread ... from RUNNING with DONE_BLOCKING`). On FEX (Proton's Wine's
  default for 32-bit code on ARM64; ours is Box64) it plays. Lesson: when a game works on one base
  and not the other, list every default that differs (here two: the address space and the 32-bit
  translator), and test them one at a time; fixing the first one uncovered the second.
- **A patch numbered by its counterpart, not by what it is written on (stage 4.10, 2026-11-25).**
  Fill (Hangover's base's 0010) was brought over to Proton's base by hand, on a source tree that
  already carried Proton's patches 0001-0020, and first saved as `patches-proton/0010`. The build
  applies the patches in name order, so it came before 0012-0020, whose changes its context lines
  include, and `git apply` failed ("patch does not apply" in `wayland_surface.c`). Renamed 0022 (and
  0011's port 0023). On the way, `pkill -f build-runtime.sh` ended its own shell again (its command
  line holds the pattern; the bracket trick, `[b]uild-runtime.sh`, avoids it). Lesson: a patch's
  number says where it is applied; a port gets the number after the last patch it was written on,
  and its header says which patch it mirrors.
- **Tested only with modes of the panel's own shape (stage 4.10, 2026-11-25).** Proton's base showed
  a game's fullscreen picture correctly since stage 3.3, checked with Ys IX at 1600x720: a 20:9 mode,
  one of those patch 0008 adds to match the panel. At a 16:9 or 4:3 mode (Need for Speed at 1280x720)
  the picture sat on the left with the wallpaper beside it, and Picture scaling did nothing: Valve's
  hack scales an emulated mode only into the area it fits, keeping its shape, and Lomiri cannot scale
  what is left. Fixed with Proton patch 0024 (fill takes emulated modes as well). Lesson: a picture
  path is tested with a size of another shape than the screen's too; the screen's own shape hides
  every scaling mistake.
