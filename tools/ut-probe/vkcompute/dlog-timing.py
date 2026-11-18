#!/usr/bin/env python3
"""UTDeck stage 1.4: summarise a dispatchlog log.txt (with the timing lines) per workload.

    dlog-timing.py log.txt [NAMES...]

A workload starts where shader modules are created after a pause in submissions (Geekbench creates
each workload's shaders when the workload starts). NAMES label the segments in order. For each
segment: wall time, submissions, dispatches, waits, and the busiest shaders with their work-group
counts and how often each configuration ran.
"""
import collections, re, sys

def main():
    path, names = sys.argv[1], sys.argv[2:]
    segs, cur = [], None
    last_q = -1e9
    cb_disp = collections.defaultdict(list)   # cb -> dispatch tuples recorded since its last submit
    for line in open(path, errors="replace"):
        k = line[:2]
        if k == "M ":
            t = float(re.search(r"t=([\d.]+)", line).group(1))
            if cur is None or (t - last_q > 300 and cur["q"]):
                cur = dict(start=t, end=t, q=0, qdur=0.0, cbs=0, polls=0, waits=collections.Counter(),
                           wdur=collections.Counter(), wlong=[], disp=0, conf=collections.Counter(),
                           shader=collections.Counter(), k=collections.Counter(), g=0, gdur=0.0, qgap=[])
                segs.append(cur)
        elif k == "D " and cur is not None:
            m = re.search(r"cb=(\w+) pipe=(\d+) shader=(\w+) spec=(\S*) groups=(\S+)", line)
            cb_disp[m.group(1)].append((m.group(3)[:10], m.group(4), m.group(5)))
        elif k == "S " and cur is not None:
            cb = re.search(r"cb=(\w+)", line).group(1)
            for d in cb_disp.pop(cb, []):
                cur["disp"] += 1; cur["conf"][d] += 1; cur["shader"][d[0]] += 1
        elif k == "K " and cur is not None:
            for f, v in re.findall(r"(\w+)=(\d+)", line)[1:]:
                cur["k"][f] += int(v)
        elif k == "Q " and cur is not None:
            t, dur = (float(x) for x in re.search(r"t=([\d.]+) dur=([\d.]+)", line).groups())
            if cur["q"]: cur["qgap"].append(t - last_q)
            cur["q"] += 1; cur["qdur"] += dur; last_q = t; cur["end"] = max(cur["end"], t + dur)
            cur["cbs"] += int(re.search(r"cbs=(\d+)", line).group(1))
            cur["polls"] += int(re.search(r"polls=(\d+)", line).group(1))
        elif k == "W " and cur is not None:
            t, dur = (float(x) for x in re.search(r"t=([\d.]+) dur=([\d.]+)", line).groups())
            w = re.search(r"what=(\w+)", line).group(1)
            cur["waits"][w] += 1; cur["wdur"][w] += dur; cur["end"] = max(cur["end"], t + dur)
            cur["wlong"].append(dur)
        elif k == "G " and cur is not None:
            cur["g"] += 1; cur["gdur"] += float(re.search(r"dur=([\d.]+)", line).group(1))
    for i, s in enumerate(segs):
        name = names[i] if i < len(names) else f"segment {i + 1}"
        wall = (s["end"] - s["start"]) / 1e3
        print(f"== {name}: {wall:.1f} s, {s['q']} submits ({s['cbs']} command buffers, {s['qdur']:.0f} ms in vkQueueSubmit), "
              f"{s['disp']} dispatches, {s['polls']} fence polls, {s['g']} query reads ({s['gdur']:.0f} ms)")
        if s["k"]: print("   also recorded:", dict(s["k"]))
        for w in s["waits"]:
            print(f"   waits {w}: {s['waits'][w]}, {s['wdur'][w]:.0f} ms in total")
        if s["wlong"]:
            wl = sorted(s["wlong"])
            print(f"   one wait: median {wl[len(wl)//2]:.2f} ms, p90 {wl[len(wl)*9//10]:.2f}, max {wl[-1]:.1f}")
        if s["qgap"]:
            g = sorted(s["qgap"])
            print(f"   time between submits: median {g[len(g)//2]:.2f} ms, p10 {g[len(g)//10]:.2f}, p90 {g[len(g)*9//10]:.2f}")
        for sh, n in s["shader"].most_common(6):
            confs = [(c, m) for c, m in s["conf"].items() if c[0] == sh]
            confs.sort(key=lambda x: -x[1])
            print(f"   shader {sh}: {n} dispatches, {len(confs)} configurations; most used: "
                  + "; ".join(f"spec={c[1]} groups={c[2]} x{m}" for c, m in confs[:3]))

if __name__ == "__main__":
    main()
