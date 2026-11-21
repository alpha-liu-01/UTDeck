# uclamp-show.py: for every Wine process (".exe" in its command line) and wineserver, the number of
# threads and how many have uclamp_min > 0 (sched_getattr).
import ctypes, os, struct
libc = ctypes.CDLL(None, use_errno=True)
for p in sorted(os.listdir("/proc"), key=lambda x: int(x) if x.isdigit() else 0):
    if not p.isdigit(): continue
    try:
        cmd = open(f"/proc/{p}/cmdline", "rb").read().replace(b"\0", b" ").decode(errors="replace")
        comm = open(f"/proc/{p}/comm").read().strip()
        ppid = open(f"/proc/{p}/stat").read().rsplit(")", 1)[1].split()[1]
    except OSError: continue
    if ".exe" not in cmd.lower() and comm != "wineserver": continue
    boosted, total = 0, 0
    for t in os.listdir(f"/proc/{p}/task"):
        buf = ctypes.create_string_buffer(56)
        if libc.syscall(275, int(t), buf, 56, 0) == 0:
            total += 1
            if struct.unpack("=IIQiIQQQII", buf.raw)[8] > 0: boosted += 1
    print(f"{p:>7} parent {ppid:>7} threads {total:>3} boosted {boosted:>3}  {(cmd.split(chr(92))[-1] or comm)[:60]}")
