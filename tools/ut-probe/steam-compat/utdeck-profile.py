#!/usr/bin/env python3
"""UTDeck stage 4.7: one Windows user folder per Steam game, where Steam Cloud looks for its saves.

    utdeck-profile.py layout DATA PREFIX USER   one prefix's user folder (utdeck-compat, every start)
    utdeck-profile.py migrate [STEAM]           every game's prefixes once (steam-tools.sh), with the
                                                cloud saves winning over the phone's (see below)
    utdeck-profile.py folders PREFIX USER       only the media folders made real (the user's own
                                                games, own-run.sh: no Steam Cloud there)

DATA is a game's steamapps/compatdata/<appid>; PREFIX its pfx (UTDeck's Wine base, whose Windows user
is the Linux user) or pfx-proton (Proton's base, user "steamuser").

Steam resolves a game's cloud folders to DATA/pfx/drive_c/users/steamuser/... (Proton's layout, with
the old Windows XP names: "My Documents", "Application Data", "Local Settings/Application Data", next
to "Saved Games" and "AppData/LocalLow"), and only for a tool whose name contains "proton". So:

- **The one real user folder** is DATA/pfx/drive_c/users/steamuser. pfx's own user folder
  (users/phablet) is a link to it, and so is pfx-proton's users/steamuser: both runtimes read and write
  the same saves (the maintainer's choice), and Steam syncs them.
- **The XP names** are links inside it: My Documents -> Documents, Application Data -> AppData/Roaming,
  Local Settings/Application Data -> AppData/Local.
- **Documents, Downloads, Music, Pictures, Videos, Desktop, Templates are real folders**: Wine links
  them to the phone's own folders (/home/phablet/Documents, ...), so a game saving to "Documents" wrote
  into the phone's Documents (Goat Simulator's "My Games/GoatSim"), shared by every game and outside
  Steam's reach. Proton makes them real folders too. Wine's prefix update (wineboot -u) leaves real
  folders alone (checked on stone, 2026-11-23).

A user folder that already has files (a prefix made before) is merged in: what the shared folder lacks
is moved there, and what both have stays as the shared folder has it, the other copy going to
DATA/utdeck-saves-<date>/<prefix>/. Nothing is deleted.

migrate, once per game (DATA/utdeck-profile): both prefixes laid out, then **the cloud wins**: each file
the game's cloud list (userdata/<account>/<appid>/remotecache.vdf) names in a Windows folder and that
was never synced to this phone (its "localtime" is 0) but exists here, made on the phone while Steam
could not sync, is moved to DATA/utdeck-saves-<date>/cloud-wins/, so Steam's next sync brings the cloud
copy without a conflict (the maintainer's choice: the PC's saves count). No account data is printed:
only app ids and file paths of the game.
"""
import glob
import os
import pwd
import re
import shutil
import sys
import time

STAMP = time.strftime("%Y%m%d")
MEDIA = ("Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos", "Templates")
XP = (("My Documents", "Documents"), ("Application Data", "AppData/Roaming"),
      ("Local Settings/Application Data", "../AppData/Local"))
# Steam's ERemoteStorageFileRoot values for Windows folders, and where they are in the user folder.
ROOTS = {"2": "My Documents", "3": "Local Settings/Application Data", "4": "Application Data",
         "9": "Saved Games", "12": "AppData/LocalLow"}
MARK = "utdeck-profile"
VERSION = "1"


def say(text):
    print(f"utdeck-profile: {text}", flush=True)


def backup_dir(data, *parts):
    return os.path.join(data, f"utdeck-saves-{STAMP}", *parts)


def merge(src, dst, spare):
    """Moves src's entries into dst where dst lacks them; what both have stays dst's, src's copy goes
    to spare. Directories are merged entry by entry. src is gone afterwards."""
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(src):
        s, d = os.path.join(src, name), os.path.join(dst, name)
        if not os.path.lexists(d):
            os.rename(s, d)
        elif os.path.isdir(s) and not os.path.islink(s) and os.path.isdir(d) and not os.path.islink(d):
            merge(s, d, os.path.join(spare, name))
        else:
            os.makedirs(spare, exist_ok=True)
            os.rename(s, os.path.join(spare, name))
    os.rmdir(src)


def real_folders(home):
    """The media folders as real folders (not links to the phone's own), and Windows' usual ones."""
    for name in MEDIA:
        p = os.path.join(home, name)
        if os.path.islink(p) and os.path.isabs(os.readlink(p)):
            os.unlink(p)
            say(f"{p}: a real folder now (was a link to the phone's {name})")
        os.makedirs(p, exist_ok=True)
    for name in ("AppData/Local", "AppData/LocalLow", "AppData/Roaming", "Saved Games"):
        os.makedirs(os.path.join(home, name), exist_ok=True)


def layout(data, prefix, user):
    shared = os.path.join(data, "pfx", "drive_c", "users", "steamuser")
    users = os.path.join(prefix, "drive_c", "users")
    home = os.path.join(users, user)
    if os.path.abspath(home) != os.path.abspath(shared):
        os.makedirs(users, exist_ok=True)
        link = os.path.relpath(shared, users)
        # A link from before (stage 4.7's first test: steamuser -> phablet) goes: the real folder
        # becomes the shared one.
        if os.path.islink(shared):
            os.unlink(shared)
        if os.path.islink(home):
            if os.readlink(home) != link:
                os.unlink(home)
        elif os.path.isdir(home):
            if not os.path.lexists(shared):
                os.makedirs(os.path.dirname(shared), exist_ok=True)
                os.rename(home, shared)
                say(f"{home}: moved to the shared user folder")
            else:
                spare = backup_dir(data, os.path.basename(prefix) + "-" + user)
                merge(home, shared, spare)
                say(f"{home}: merged into the shared user folder (copies it had too: {spare})")
        os.makedirs(shared, exist_ok=True)
        if not os.path.lexists(home):
            os.symlink(link, home)
    os.makedirs(shared, exist_ok=True)
    real_folders(shared)
    for name, target in XP:
        p = os.path.join(shared, name)
        if not os.path.lexists(p):
            os.makedirs(os.path.dirname(p), exist_ok=True)
            os.symlink(target, p)
    return shared


def remote_files(steam, appid):
    """(root, path, localtime) of each file in the game's cloud lists (every account on the phone)."""
    out = []
    for f in glob.glob(os.path.join(steam, "userdata", "*", appid, "remotecache.vdf")):
        text = open(f, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r'^\t"([^"\n]+)"\n\t\{\n(.*?)^\t\}', text, re.M | re.S):
            fields = dict(re.findall(r'^\t\t"([^"]+)"\t\t"([^"]*)"', m.group(2), re.M))
            out.append((fields.get("root", ""), m.group(1), fields.get("localtime", "")))
    return out


def cloud_wins(steam, data, appid, shared):
    moved = 0
    for root, path, localtime in remote_files(steam, appid):
        if root not in ROOTS or localtime != "0" or ".." in path.split("/"):
            continue
        local = os.path.join(shared, ROOTS[root], path)
        if os.path.isfile(local):
            spare = backup_dir(data, "cloud-wins", ROOTS[root], path)
            os.makedirs(os.path.dirname(spare), exist_ok=True)
            os.rename(local, spare)
            moved += 1
    if moved:
        say(f"app {appid}: {moved} phone files the cloud also has moved to {backup_dir(data, 'cloud-wins')}")


def migrate(steam):
    user = os.environ.get("USER") or pwd.getpwuid(os.getuid()).pw_name
    for data in sorted(glob.glob(os.path.join(steam, "steamapps", "compatdata", "*"))):
        appid = os.path.basename(data)
        if not appid.isdigit() or appid == "0":
            continue
        mark = os.path.join(data, MARK)
        if os.path.isfile(mark) and open(mark).read().strip() == VERSION:
            continue
        shared = None
        if os.path.isdir(os.path.join(data, "pfx", "drive_c", "users")):
            shared = layout(data, os.path.join(data, "pfx"), user)
        if os.path.isdir(os.path.join(data, "pfx-proton", "drive_c", "users")):
            shared = layout(data, os.path.join(data, "pfx-proton"), "steamuser")
        if shared:
            cloud_wins(steam, data, appid, shared)
        with open(mark, "w") as f:
            f.write(VERSION + "\n")
        say(f"app {appid}: done")


def main():
    a = sys.argv[1:]
    if a[:1] == ["layout"] and len(a) == 4:
        layout(a[1], a[2], a[3])
    elif a[:1] == ["migrate"]:
        migrate(a[1] if len(a) > 1 else os.path.expanduser("~/.local/share/Steam"))
    elif a[:1] == ["folders"] and len(a) == 3:
        real_folders(os.path.join(a[1], "drive_c", "users", a[2]))
    else:
        sys.exit(__doc__)


main()
