#!/usr/bin/env python3
"""cputime.py OUTFILE -- CMD...: run CMD with its output in OUTFILE; print wall and CPU time of CMD and
its waited-for children (not wineserver, which detaches)."""
import resource, subprocess, sys, time
out = sys.argv[1]; cmd = sys.argv[sys.argv.index('--') + 1:]
t0 = time.time()
with open(out, 'w') as f:
    rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
wall = time.time() - t0
ru = resource.getrusage(resource.RUSAGE_CHILDREN)
print(f"exit {rc}, wall {wall:.2f} s, cpu {ru.ru_utime + ru.ru_stime:.2f} s (user {ru.ru_utime:.2f}, sys {ru.ru_stime:.2f}), max RSS {ru.ru_maxrss // 1024} MB")
