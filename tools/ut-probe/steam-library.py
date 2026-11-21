#!/usr/bin/env python3
"""UTDeck stage 3.8: one entry in Lomiri's app drawer per installed Steam game.

    steam-library.py          write ~/.local/share/applications/utdeck-game-<appid>.desktop for each
                              installed game and remove the entries of games no longer installed
    steam-library.py --show   print the games it finds, write nothing

Runs on the phone (steam-service.sh start runs it). Reads only Steam's install records
(steamapps/appmanifest_<appid>.acf: name and state) and Steam's image cache (the game's cover as
its icon); no account data. Tapping an entry starts "steam-service.sh play APPID" as a transient
user service and returns at once: the job starts hidden Steam if needed, waits for it to sign in,
then starts the game.
"""
import os
import re
import sys

HOME = os.path.expanduser("~")
STEAM = os.path.join(HOME, ".local/share/Steam")
APPS = os.path.join(HOME, ".local/share/applications")
SERVICE = os.path.join(HOME, "utdeck-probe/steam/steam-service.sh")
PREFIX = "utdeck-game-"

# Installed by Steam but not games: its runtimes, Proton, the shared redistributables, FEX.
TOOL_IDS = {"228980", "1070560", "1391110", "1628350", "4183110", "3127680", "1493710"}
TOOL_NAMES = re.compile(r"^(Steam Linux Runtime|Proton|Steamworks Common Redistributables|FEX$|SteamVR)")


def manifest_fields(path):
    fields = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r'\s*"(\w+)"\s+"([^"]*)"', line)
            if m and m.group(1) not in fields:   # the first one: the app's own, not a depot's
                fields[m.group(1)] = m.group(2)
    return fields


def installed_games():
    games = []
    folder = os.path.join(STEAM, "steamapps")
    for name in sorted(os.listdir(folder)):
        m = re.match(r"appmanifest_(\d+)\.acf$", name)
        if not m:
            continue
        f = manifest_fields(os.path.join(folder, name))
        appid, title = m.group(1), f.get("name", "")
        if appid in TOOL_IDS or TOOL_NAMES.match(title):
            continue
        if not int(f.get("StateFlags", "0")) & 4:   # 4: fully installed
            continue
        games.append((appid, title))
    return games


def icon_for(appid):
    # As UTDeck's app (app/src/Library.cpp): newer Steam clients keep each picture in a folder named
    # after its hash (librarycache/<id>/<hash>/...), and the portrait cover there is
    # library_capsule.jpg. Portrait first, then the wide header, then the logo; the newest copy wins.
    cache = os.path.join(STEAM, "appcache/librarycache", appid)
    if os.path.isdir(cache):
        folders = [cache] + [e.path for e in os.scandir(cache) if e.is_dir()]
        for names in (("library_600x900.jpg", "library_capsule.jpg"),
                      ("header.jpg", "library_header.jpg"), ("logo.png",)):
            found = [os.path.join(d, n) for d in folders for n in names
                     if os.path.isfile(os.path.join(d, n))]
            if found:
                return max(found, key=os.path.getmtime)
    return os.path.join(STEAM, "public/steam_tray.ico")


def entry(appid, title):
    # Desktop entry values are single lines; Exec needs no quoting for these plain arguments.
    title = title.replace("\n", " ").strip()
    return "\n".join([
        "[Desktop Entry]",
        "Type=Application",
        f"Name={title}",
        "Comment=Steam game, started through UTDeck",
        f"Exec=systemd-run --user --collect --quiet --unit=utdeck-play-{appid} {SERVICE} play {appid}",
        f"Icon={icon_for(appid)}",
        "Terminal=false",
        "X-Lomiri-Touch=true",
        "",
    ])


def main():
    games = installed_games()
    if sys.argv[1:] == ["--show"]:
        for appid, title in games:
            print(f"{appid}  {title}")
        return
    os.makedirs(APPS, exist_ok=True)
    wanted = set()
    for appid, title in games:
        path = os.path.join(APPS, f"{PREFIX}{appid}.desktop")
        wanted.add(os.path.basename(path))
        text = entry(appid, title)
        try:
            with open(path, encoding="utf-8") as f:
                if f.read() == text:
                    continue
        except FileNotFoundError:
            pass
        with open(path + ".new", "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(path + ".new", path)
        print(f"entry: {title} ({appid})")
    for name in os.listdir(APPS):
        if name.startswith(PREFIX) and name.endswith(".desktop") and name not in wanted:
            os.remove(os.path.join(APPS, name))
            print(f"removed: {name}")


if __name__ == "__main__":
    main()
