"""dmpinfo.py FILE.dmp: the exception (code, address, module + offset) and the faulting thread's
registers from a Windows minidump. Reads only the exception, module list and thread context."""
import struct, sys
d = open(sys.argv[1], "rb").read()
sig, ver, nstreams, dir_rva = struct.unpack_from("<IIII", d, 0)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from("<III", d, dir_rva + 12 * i)
    streams[t] = (size, rva)
mods = []
if 4 in streams:
    size, rva = streams[4]
    n = struct.unpack_from("<I", d, rva)[0]
    for i in range(n):
        o = rva + 4 + 108 * i
        base, msize = struct.unpack_from("<QI", d, o)
        name_rva = struct.unpack_from("<I", d, o + 20)[0]
        ln = struct.unpack_from("<I", d, name_rva)[0]
        mods.append((base, msize, d[name_rva + 4:name_rva + 4 + ln].decode("utf-16-le").split("\\")[-1]))
def where(a):
    for base, size, name in mods:
        if base <= a < base + size:
            return f"{name}+{a - base:#x}"
    return "?"
size, rva = streams[6]
tid = struct.unpack_from("<I", d, rva)[0]
code, flags, rec, addr, nparams = struct.unpack_from("<IIQQI", d, rva + 8)
params = struct.unpack_from("<15Q", d, rva + 8 + 32)[:nparams]
print(f"thread {tid:#x}: exception {code:#010x} at {addr:#x} ({where(addr)}), params {[hex(p) for p in params]}")
csize, crva = struct.unpack_from("<II", d, rva + 8 + 152)
ctx = d[crva:crva + csize]
if csize >= 0x4d0:   # x86-64 CONTEXT
    names = ["rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8","r9","r10","r11","r12","r13","r14","r15","rip"]
    regs = struct.unpack_from("<17Q", ctx, 0x78)
    print(" ".join(f"{n}={v:#x}" for n, v in zip(names, regs)))
    print("rip in", where(regs[16]))
print("modules:", len(mods), "; game and runtime modules near the fault:")
for base, size, name in sorted(mods):
    if name.lower() in ("ys9.exe", "d3d11.dll", "dxgi.dll", "ntdll.dll", "kernelbase.dll", "gfsdk_ssao_d3d11.win64.dll", "steam_api64.dll", "solstice.dll"):
        print(f"  {base:#x} {size:#x} {name}")
# the faulting thread's stack: values that point into a module (return addresses, most likely)
if 3 in streams:
    size, rva = streams[3]
    n = struct.unpack_from("<I", d, rva)[0]
    for i in range(n):
        o = rva + 4 + 48 * i
        t = struct.unpack_from("<I", d, o)[0]
        if t != tid:
            continue
        start, msize, mrva = struct.unpack_from("<QII", d, o + 24)
        rsp = regs[4]
        print(f"stack {start:#x}+{msize:#x}; values from rsp that point into modules:")
        shown = 0
        for off in range(max(0, rsp - start), msize - 7, 8):
            v = struct.unpack_from("<Q", d, mrva + off)[0]
            w = where(v)
            if w != "?":
                print(f"  [rsp+{start + off - rsp:#x}] {v:#x} {w}")
                shown += 1
                if shown >= 25:
                    break
