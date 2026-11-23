#!/usr/bin/env python3
"""UTDeck stage 4.7: read Steam's appinfo.vdf, its local copy of each app's public data. Reads only.

    appinfo.py [--library] [APPID...]     one line per app, tab-separated:
                                          appid, type, systems, Windows size in MB, name, cloud

--library: the apps in Steam's library cache (appcache/librarycache/<appid>/: Steam caches the art of
every game in the user's library), the user's games; with APPIDs, only those. Without either, every
app in the file. No account data is in this file: names, types, systems (oslist), depots, cloud.

cloud (stage 4.7): how the game uses Steam Cloud, from its "ufs" block: "auto" (Steam syncs files of
the game's folders by rules, "savefiles": the Windows ones need the prefix's user folder), "api" (the
game hands its saves to Steam itself: a quota, no rules), or empty (no Steam Cloud).

Format (versions 28 and 29, magic 0x07564428 / 0x07564429): a header (magic, universe; version 29 adds
the offset of a table of key names at the end of the file), then per app: appid, size, info state, last
update, access token (8 bytes), SHA-1 (20), change number, binary SHA-1 (20), and the app's data as
binary key-values: a type byte (0 nested, 1 string, 2 int32, 3 float, 7 uint64, 8 end), the key (version
29: a 4-byte index into the name table; before: a NUL-terminated string), the value.
"""
import os
import struct
import sys

STEAM = os.path.expanduser("~/.local/share/Steam")


def read_kv(buf, pos, names):
    obj = {}
    while True:
        kind = buf[pos]
        pos += 1
        if kind == 8 or kind == 0x0b:
            return obj, pos
        if names is not None:
            key = names[struct.unpack_from("<I", buf, pos)[0]]
            pos += 4
        else:
            end = buf.index(b"\0", pos)
            key = buf[pos:end].decode("utf-8", "replace")
            pos = end + 1
        if kind == 0:
            value, pos = read_kv(buf, pos, names)
        elif kind == 1:
            end = buf.index(b"\0", pos)
            value = buf[pos:end].decode("utf-8", "replace")
            pos = end + 1
        elif kind in (2, 4, 6):
            value = struct.unpack_from("<i", buf, pos)[0]
            pos += 4
        elif kind == 3:
            value = struct.unpack_from("<f", buf, pos)[0]
            pos += 4
        elif kind in (7, 10):
            value = struct.unpack_from("<Q", buf, pos)[0]
            pos += 8
        else:
            raise ValueError(f"unknown value type {kind} at {pos - 1}")
        obj[key] = value


def apps(path):
    buf = open(path, "rb").read()
    magic, _universe = struct.unpack_from("<II", buf, 0)
    if magic == 0x07564429:
        table = struct.unpack_from("<q", buf, 8)[0]
        count = struct.unpack_from("<I", buf, table)[0]
        names, pos = [], table + 4
        for _ in range(count):
            end = buf.index(b"\0", pos)
            names.append(buf[pos:end].decode("utf-8", "replace"))
            pos = end + 1
        pos = 16
    elif magic == 0x07564428:
        names, pos = None, 8
    else:
        sys.exit(f"appinfo.vdf: unknown format {magic:#x}")
    while True:
        appid = struct.unpack_from("<I", buf, pos)[0]
        if appid == 0:
            return
        size = struct.unpack_from("<I", buf, pos + 4)[0]
        start = pos + 8
        data, _ = read_kv(buf, start + 60, names)   # after state, update, token, SHA-1, change, SHA-1
        yield appid, data.get("appinfo", data)
        pos = start + size


def windows_size(info):
    """The Windows build's size in MB: its depots for Windows (or for every system), public branch."""
    total = 0
    for depot in info.get("depots", {}).values():
        if not isinstance(depot, dict) or "manifests" not in depot:
            continue
        oslist = depot.get("config", {}).get("oslist", "") if isinstance(depot.get("config"), dict) else ""
        if oslist and "windows" not in oslist:
            continue
        public = depot["manifests"].get("public")
        if isinstance(public, dict):
            total += int(public.get("size", 0) or 0)
    return total // (1024 * 1024)


def cloud(info):
    ufs = info.get("ufs", {})
    if not isinstance(ufs, dict):
        return ""
    if isinstance(ufs.get("savefiles"), dict) and ufs["savefiles"]:
        return "auto"
    try:
        return "api" if int(ufs.get("quota", 0) or 0) > 0 else ""
    except (TypeError, ValueError):
        return ""


def main():
    args = sys.argv[1:]
    wanted = {int(a) for a in args if a.isdigit()}
    if "--library" in args:
        cache = os.path.join(STEAM, "appcache/librarycache")
        library = {int(d) for d in os.listdir(cache) if d.isdigit()}
        wanted = (wanted & library) if wanted else library
    for appid, info in apps(os.path.join(STEAM, "appcache/appinfo.vdf")):
        if wanted and appid not in wanted:
            continue
        common = info.get("common", {})
        print("\t".join([str(appid), str(common.get("type", "")).lower(), common.get("oslist", ""),
                         str(windows_size(info)), common.get("name", ""), cloud(info)]))


main()
