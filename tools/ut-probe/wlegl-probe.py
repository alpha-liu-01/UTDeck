#!/usr/bin/env python3
"""UTDeck stage 0: does the compositor hand out gralloc buffers over android_wlegl?

A native (glibc) Mesa Turnip cannot present on Lomiri the usual way: Lomiri offers no
zwp_linux_dmabuf_v1. The Droidian Mesa patch "vulkan/wsi/wayland: present through
android_wlegl gralloc buffers" (panxiao81/debian-mesa adb4b85) asks the compositor for a
buffer with android_wlegl.get_server_buffer_handle (version 2), then imports the first file
descriptor as a dma-buf and blits the frame into it. This probe makes the same request, with
the same format and usage values, and reports what comes back.

It speaks the Wayland wire protocol directly (plain Python, no libraries): the phone has no
compiler, no wayland-info and no Python Wayland bindings.

Run as phablet in the Lomiri session:
  python3 wlegl-probe.py alloc            # ask for buffers, check fds, size, stride, mmap
  python3 wlegl-probe.py show [seconds]   # also show a test pattern in a fullscreen window
                                          # (screen on and unlocked)

Socket: $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY, default /run/user/32011/wayland-0.
"""
import array
import fcntl
import mmap
import os
import select
import socket
import struct
import sys
import time

# Android HAL pixel formats (system/graphics.h) and gralloc usage bits, as the patch uses them
HAL_RGBA_8888, HAL_RGBX_8888, HAL_BGRA_8888 = 1, 2, 5
USAGE_HW_TEXTURE, USAGE_HW_RENDER = 0x100, 0x200
DMA_BUF_IOCTL_SYNC = 0x40086200
DMA_BUF_SYNC_READ, DMA_BUF_SYNC_WRITE, DMA_BUF_SYNC_END = 1, 2, 4


class Wayland:
    """A minimal Wayland client: requests are packed by hand, events are parsed by signature."""

    def __init__(self):
        path = os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/run/user/32011"),
                            os.environ.get("WAYLAND_DISPLAY", "wayland-0"))
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.next_id = 2
        self.objects = {1: ("wl_display", {})}  # id -> (interface, {opcode: (name, sig, handler)})
        self.buf = b""
        self.fds = []
        self.errors = []
        self.on(1, "wl_display", {0: ("error", "ous", self._error), 1: ("delete_id", "u", None)})

    def _error(self, obj, code, msg):
        self.errors.append((obj, code, msg))
        print(f"  PROTOCOL ERROR: object {obj} code {code}: {msg}")

    def on(self, oid, iface, events):
        self.objects[oid] = (iface, events)

    def new_id(self):
        oid = self.next_id
        self.next_id += 1
        return oid

    def send(self, oid, opcode, payload=b"", fds=()):
        msg = struct.pack("<II", oid, ((8 + len(payload)) << 16) | opcode) + payload
        anc = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", fds))] if fds else []
        self.sock.sendmsg([msg], anc)

    @staticmethod
    def string(s):
        b = s.encode() + b"\0"
        return struct.pack("<I", len(b)) + b + b"\0" * (-len(b) % 4)

    def read(self, timeout=2.0):
        r, _, _ = select.select([self.sock], [], [], timeout)
        if not r:
            return False
        data, anc, _, _ = self.sock.recvmsg(65536, socket.CMSG_SPACE(28 * 4))
        if not data:
            raise ConnectionError("compositor closed the connection")
        for level, kind, cdata in anc:
            if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                a = array.array("i")
                a.frombytes(cdata[:len(cdata) - len(cdata) % 4])
                self.fds.extend(a)
        self.buf += data
        while len(self.buf) >= 8:
            oid, word = struct.unpack_from("<II", self.buf)
            size = word >> 16
            if len(self.buf) < size:
                break
            body, self.buf = self.buf[8:size], self.buf[size:]
            self._dispatch(oid, word & 0xFFFF, body)
        return True

    def _dispatch(self, oid, opcode, body):
        iface, events = self.objects.get(oid, ("?", {}))
        name, sig, handler = events.get(opcode, (f"event{opcode}", "", None))
        args, pos = [], 0
        for t in sig:
            if t in "uion":
                args.append(struct.unpack_from("<i" if t == "i" else "<I", body, pos)[0])
                pos += 4
            elif t in "sa":
                n = struct.unpack_from("<I", body, pos)[0]
                raw = body[pos + 4:pos + 4 + n]
                args.append(raw[:-1].decode(errors="replace") if t == "s" else raw)
                pos += 4 + n + (-n % 4)
            elif t == "h":
                args.append(self.fds.pop(0) if self.fds else -1)
        if handler:
            handler(*args)

    def roundtrip(self, timeout=3.0):
        cb = self.new_id()
        done = []
        self.on(cb, "wl_callback", {0: ("done", "u", lambda _s: done.append(1))})
        self.send(1, 0, struct.pack("<I", cb))  # wl_display.sync
        end = time.time() + timeout
        while not done and time.time() < end:
            self.read(max(0.0, end - time.time()))
        return bool(done)

    def bind(self, name, iface, version):
        oid = self.new_id()
        self.send(self.registry, 0, struct.pack("<I", name) + self.string(iface) +
                  struct.pack("<II", version, oid))
        return oid

    def connect_registry(self):
        self.globals = {}
        self.registry = self.new_id()
        self.on(self.registry, "wl_registry", {
            0: ("global", "usu", lambda n, i, v: self.globals.__setitem__(i, (n, v))),
            1: ("global_remove", "u", None)})
        self.send(1, 1, struct.pack("<I", self.registry))  # wl_display.get_registry
        self.roundtrip()


def fd_info(fd):
    try:
        target = os.readlink(f"/proc/self/fd/{fd}")
    except OSError as e:
        target = f"? ({e})"
    try:
        size = os.lseek(fd, 0, os.SEEK_END)
        os.lseek(fd, 0, os.SEEK_SET)
    except OSError:
        size = -1
    return target, size


def request_buffer(wl, wlegl, width, height, fmt, usage):
    """android_wlegl.get_server_buffer_handle; returns what the compositor sent back."""
    res = {"fds": [], "ints": None, "buffer": None, "format": None, "stride": None}
    handle = wl.new_id()

    def on_buffer(buf_id, f, stride):
        res.update(buffer=buf_id, format=f, stride=stride)
        wl.on(buf_id, "wl_buffer", {0: ("release", "", None)})

    wl.on(handle, "android_wlegl_server_buffer_handle", {
        0: ("buffer_fd", "h", lambda fd: res["fds"].append(fd)),
        1: ("buffer_ints", "a", lambda a: res.__setitem__("ints", list(array.array("i", a)))),
        2: ("buffer", "nii", on_buffer)})
    wl.send(wlegl, 2, struct.pack("<Iiiii", handle, width, height, fmt, usage))
    end = time.time() + 3
    while res["buffer"] is None and not wl.errors and time.time() < end:
        wl.read(max(0.0, end - time.time()))
    wl.roundtrip()
    return res


def check_mmap(fd, size, stride, height, write_pattern=False):
    """Map the pixel buffer (what a CPU fallback would do) and optionally draw color bars."""
    try:
        m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    except OSError as e:
        return f"mmap failed: {e}"
    try:
        fcntl.ioctl(fd, DMA_BUF_IOCTL_SYNC, struct.pack("<Q", DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE))
        sync = "DMA_BUF_IOCTL_SYNC ok"
    except OSError as e:
        sync = f"DMA_BUF_IOCTL_SYNC failed: {e}"
    if write_pattern:
        bars = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
                (255, 0, 255), (255, 0, 0), (0, 0, 255), (16, 16, 16)]  # RGB
        width = stride
        row = bytearray()
        for x in range(width):
            r, g, b = bars[x * len(bars) // width]
            row += bytes((r, g, b, 255))  # RGBA_8888 byte order
        row = bytes(row)
        for y in range(height):
            m[y * stride * 4:(y + 1) * stride * 4] = row
    try:
        fcntl.ioctl(fd, DMA_BUF_IOCTL_SYNC, struct.pack("<Q", DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ |
                                                        DMA_BUF_SYNC_WRITE))
    except OSError:
        pass
    m.close()
    return f"mmap ok, {sync}" + (", pattern written" if write_pattern else "")


def show_window(wl, buffer_id, seconds):
    """A fullscreen xdg_toplevel with the compositor's own gralloc buffer attached."""
    comp = wl.bind(wl.globals["wl_compositor"][0], "wl_compositor", 4)
    wm = wl.bind(wl.globals["xdg_wm_base"][0], "xdg_wm_base", 1)
    wl.on(wm, "xdg_wm_base", {0: ("ping", "u", lambda s: wl.send(wm, 3, struct.pack("<I", s)))})
    surf = wl.new_id()
    wl.send(comp, 0, struct.pack("<I", surf))  # create_surface
    wl.on(surf, "wl_surface", {0: ("enter", "o", None), 1: ("leave", "o", None)})
    xs = wl.new_id()
    wl.send(wm, 2, struct.pack("<II", xs, surf))  # get_xdg_surface
    configured = []
    wl.on(xs, "xdg_surface", {0: ("configure", "u", lambda s: (configured.append(s),
                                                                 wl.send(xs, 4, struct.pack("<I", s))))})
    tl = wl.new_id()
    sizes = []
    wl.send(xs, 1, struct.pack("<I", tl))  # get_toplevel
    wl.on(tl, "xdg_toplevel", {0: ("configure", "iia", lambda w, h, st: sizes.append((w, h))),
                               1: ("close", "", None)})
    wl.send(tl, 2, wl.string("UTDeck stage 0 wlegl probe"))  # set_title
    wl.send(tl, 3, wl.string("utdeck-wlegl-probe"))  # set_app_id
    wl.send(surf, 6)  # commit without a buffer: ask for the first configure
    end = time.time() + 5
    while not configured and time.time() < end:
        wl.read(0.5)
    wl.send(tl, 11, struct.pack("<I", 0))  # set_fullscreen(output = null)
    wl.send(surf, 1, struct.pack("<Iii", buffer_id, 0, 0))  # attach
    wl.send(surf, 9, struct.pack("<iiii", 0, 0, 0x7FFFFFFF, 0x7FFFFFFF))  # damage_buffer
    wl.send(surf, 6)  # commit
    wl.roundtrip()
    print(f"  window: configures {sizes}, xdg configure serials {len(configured)}")
    print(f"  showing for {seconds} s: look for eight vertical color bars "
          "(white, yellow, cyan, green, magenta, red, blue, black)")
    end = time.time() + seconds
    while time.time() < end and not wl.errors:
        wl.read(0.5)
    return not wl.errors


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "alloc"
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    wl = Wayland()
    wl.connect_registry()
    if "android_wlegl" not in wl.globals:
        print("android_wlegl: not offered")
        return 1
    name, version = wl.globals["android_wlegl"]
    print(f"android_wlegl: offered, version {version}")
    if version < 2:
        print("  get_server_buffer_handle needs version 2")
        return 1
    print(f"zwp_linux_dmabuf_v1 offered: {'zwp_linux_dmabuf_v1' in wl.globals}")
    wlegl = wl.bind(name, "android_wlegl", 2)
    usage = USAGE_HW_TEXTURE | USAGE_HW_RENDER
    ok = True
    shown = None
    for width, height, fmt, label in [(1080, 2400, HAL_RGBA_8888, "RGBA_8888"),
                                      (1080, 2400, HAL_BGRA_8888, "BGRA_8888"),
                                      (1280, 720, HAL_RGBA_8888, "RGBA_8888"),
                                      (2400, 1080, HAL_RGBX_8888, "RGBX_8888")]:
        print(f"\nget_server_buffer_handle {width}x{height} {label} usage 0x{usage:x}:")
        res = request_buffer(wl, wlegl, width, height, fmt, usage)
        if wl.errors:
            ok = False
            break
        if res["buffer"] is None:
            print("  no buffer event (timeout)")
            ok = False
            continue
        print(f"  buffer: wl_buffer id 0x{res['buffer']:x}, format {res['format']}, "
              f"stride {res['stride']} px, {len(res['fds'])} fds, {len(res['ints'] or [])} ints")
        for i, fd in enumerate(res["fds"]):
            target, size = fd_info(fd)
            print(f"  fd[{i}]: {target}, size {size}")
        if res["fds"]:
            need = res["stride"] * 4 * height
            _, size = fd_info(res["fds"][0])
            fits = size >= need
            print(f"  patch's size check (fd[0] size >= stride*4*height = {need}): "
                  f"{'pass' if fits else 'FAIL'}")
            ok &= fits
            want_show = mode == "show" and shown is None and fmt == HAL_RGBA_8888 and width == 1080
            print("  " + check_mmap(res["fds"][0], size, res["stride"], height, want_show))
            if want_show:
                shown = res["buffer"]
        for fd in res["fds"]:
            if res["buffer"] != shown:
                os.close(fd)
    if shown and not wl.errors:
        print("\nshow:")
        ok &= show_window(wl, shown, seconds)
    print("\nresult:", "PASS" if ok and not wl.errors else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
