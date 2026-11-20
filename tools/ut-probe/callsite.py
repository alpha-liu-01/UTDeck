"""callsite.py DLL RVA...: for each return address RVA, decode an x64 `call [rip+disp32]` (FF 15)
just before it and print the imported function it calls. Reads only."""
import struct, sys
d = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", d, 0x3c)[0]
nsec, = struct.unpack_from("<H", d, pe + 6)
opt = pe + 24; optsize, = struct.unpack_from("<H", d, pe + 20)
secs = [struct.unpack_from("<IIII", d, opt + optsize + 40 * i + 8) for i in range(nsec)]
def off(rva):
    for vs, va, rs, rp in secs:
        if va <= rva < va + max(vs, rs): return rp + rva - va
imp_rva, = struct.unpack_from("<I", d, opt + 112 + 8)
iat = {}
o = off(imp_rva)
while True:
    oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", d, o)
    if not name_rva: break
    n = off(name_rva); dll = d[n:d.index(b"\0", n)].decode()
    t = off(oft or ft); k = 0
    while True:
        e, = struct.unpack_from("<Q", d, t + 8 * k)
        if not e: break
        fn = f"#{e & 0xffff}" if e >> 63 else d[off(e) + 2:d.index(b"\0", off(e) + 2)].decode()
        iat[ft + 8 * k] = f"{dll}!{fn}"; k += 1
    o += 20
for a in sys.argv[2:]:
    r = int(a, 16); b = d[off(r) - 6:off(r)]
    if b[:2] == b"\xff\x15":
        disp, = struct.unpack_from("<i", b, 2)
        print(f"{a}: call [{r + disp:#x}] = {iat.get(r + disp, '?')}")
    else:
        print(f"{a}: not an indirect call: {b.hex()}")
