#!/bin/sh
# UTDeck stage 1.2: run Geekbench 6's Vulkan test on the phone, unattended.
#
#   gb-vk.sh <label> [VAR=value ...]
#
# Start it as a user unit, so an adbd restart cannot kill it:
#   systemd-run --user --unit=utdeck-gb-<label> --collect [--setenv=ICD=...] gb-vk.sh <label> ...
#
# Environment:
#   ICD=<folder>   Turnip build to use, a folder in the test folder with turnip_icd.json
#                  (default turnip-wlegl); ICD=vendor uses the vendor driver through libhybris.
#   STOP_AFTER="<workload name>"  end Geekbench as soon as the workload after it starts (no upload).
#   TRACE=1        run under kgsl-trace; the KGSL log goes to gb-<label>.trace.
# VAR=value arguments are exported (driver switches such as TU_DEBUG=... or IR3_SHADER_DEBUG=...).
# Writes gb-<label>.log (Geekbench's output) and gb-<label>.status (summary) in the test folder.
label=$1; shift
P=/home/phablet/utdeck-probe
cd $P
export GLIBC_TUNABLES=glibc.rtld.optional_static_tls=4096 LD_PRELOAD=/usr/lib/aarch64-linux-gnu/libtls-padding.so
if [ "${ICD:-}" = vendor ]; then
  unset LD_LIBRARY_PATH VK_DRIVER_FILES
else
  export LD_LIBRARY_PATH=$P/vk/lib VK_DRIVER_FILES=$P/${ICD:-turnip-wlegl}/turnip_icd.json
fi
for kv in "$@"; do export "$kv"; done
t() { for z in /sys/class/thermal/thermal_zone*; do n=$(cat $z/type); case $n in cpuss-0-usr|gpuss-0-usr) printf "%s=%s " $n $(cat $z/temp);; esac; done; }
{
echo "label=$label driver=${ICD:-turnip-wlegl} stop_after=${STOP_AFTER:-} trace=${TRACE:-} extra=$*"
echo "before: $(t) $(date +%T)"
cd $P/Geekbench-6.7.1-LinuxARMPreview
if [ -n "${TRACE:-}" ]; then
  $P/vkcompute/kgsl-trace -o $P/gb-$label.trace -- ./geekbench6 --gpu Vulkan > $P/gb-$label.log 2>&1 &
else
  ./geekbench6 --gpu Vulkan > $P/gb-$label.log 2>&1 &
fi
gb=$!
if [ -n "${STOP_AFTER:-}" ]; then
  while kill -0 $gb 2>/dev/null; do
    if [ "$(grep -A100000 "Running $STOP_AFTER" $P/gb-$label.log | grep -c -E "^  Running ")" -ge 2 ]; then
      pkill -f geekbench_aarch64; echo "stopped after $STOP_AFTER"; break
    fi
    sleep 2
  done
fi
wait $gb; rc=$?
echo "exit $rc after: $(t) $(date +%T)"
grep -E "failed validation|returned -4|compute/[0-9]+$" $P/gb-$label.log | sort -u | head -4
dmesg | grep -i "kgsl.*geekbench" | tail -3
} > $P/gb-$label.status 2>&1
