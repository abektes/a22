#!/usr/bin/env python3
"""Projects as files over USB (docs/PLAN-projects.md P4): fm1-base's companion (device/companion.c) talking to A22's
APP_FILES hooks (app/app.c), in the host simulator driven as an FM-1 (a22_host --serve) by tools/fm1_companion.py's
own client, as the companion page and the tool talk to an FM-1.

  the list      project1.a22..project4.a22, kind project, P1's size once it is saved from the panel, the rest empty
  read          P1 read whole, CRC-checked: its header (magic "A22P", project 0) and its state as the slot holds it
  write         P1's file put to project2: P2 is P1's state and loop, its header says project 1 now; a restart keeps it
  refused       a body that does not match its header's CRC, a header of another version: P3 stays empty
  cut short     a put given up halfway leaves P4 empty, and the next put of it goes in whole
  busy          a put while the tape plays is refused (exit code 5 for the tool), and goes in once it is stopped
  delete        P2 deleted: empty, the others kept (after a restart too: tests/projects.txt)
  the panel wins  a panel save while a put holds the last free slot: the put is given up, the save goes in, the
                tape's ring untouched; the put resumed is refused, and sent again from the start goes in

Run by tests/run_tests.sh after ./build.sh --host: python3 tests/projfiles.py app/build/host/a22_host
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "tools"))
import fm1_companion as C  # noqa: E402

EDIT, SAVE, PLAY, REC = 0x10, 0x80, 0x400, 0x800
fails = 0


def check(cond, what):
    global fails
    print(("  ok   " if cond else "FAIL ") + what)
    fails += not cond


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "app/build/host/a22_host"
    link = C.ServeLink([host, "--serve", os.path.dirname(os.path.abspath(host))])   # (its midi_out.txt there)
    c = C.Client(link)

    def run(ms):
        link.cmd(f"run {ms}")

    def expect(what, val):
        out = link.cmd(f"expect {what} {val}")
        check(not any(s.startswith("FAIL") for s in out), f"expect {what} {val}")

    def tap(mask):
        link.cmd(f"buttons {mask}")
        run(80)
        link.cmd("buttons 0")
        run(80)

    def key(k, ms=300):
        link.cmd(f"key {k} 100")
        run(ms)
        link.cmd(f"key {k} 0")
        run(100)

    def panel_save(k):                           # SAVE + EDIT + the project's black key
        link.cmd(f"buttons {SAVE | EDIT}")
        run(60)
        key(k, 60)
        link.cmd("buttons 0")
        run(1200)

    run(600)
    tap(REC)
    key(12)
    tap(REC)
    run(1500)
    tap(PLAY)
    run(1500)
    panel_save(1)
    expect("proj.1", 0)

    # the list
    fs = c.files()
    check([f["name"] for f in fs] == [f"project{n}.a22" for n in range(1, 5)], "four files, project1.a22..project4.a22")
    check(all(f["kind"] == "project" and f["room"] == 20480 for f in fs), "kind project, room 20,480 bytes")
    check(fs[0]["size"] > 256 and all(f["size"] == 0 for f in fs[1:]), f"P1 holds {fs[0]['size']} bytes, the rest none")

    # read
    p1 = c.read(0)
    magic, ver, proj, seq, sl, ll = struct.unpack_from("<IIIIII", p1)
    check(magic == 0x50323241 and ver == 1 and proj == 0, "P1's header: A22P, version 1, project 0")
    check(len(p1) == 256 + ((sl + 255) & ~255) + ll and sl == 2888, f"P1: state {sl} B, loop {ll} B, as the file's size")

    # write: P1's file as project2
    c.write(1, p1)
    run(100)
    expect("proj_same", 2)
    p2 = c.read(1)
    check(p2[256:] == p1[256:], "project2 reads back with P1's body")
    check(struct.unpack_from("<I", p2, 8)[0] == 1, "and its header says project 1 (the second)")
    expect("toast", "P2_FROM_USB")

    # refused: a body that does not match, a version this firmware does not know
    bad = bytearray(p1)
    bad[300] ^= 0xFF
    try:
        c.write(2, bytes(bad))
        check(False, "a damaged body is refused")
    except C.CompanionError:
        check(True, "a damaged body is refused")
    bad = bytearray(p1)
    bad[4] = 2
    try:
        c.write(2, bytes(bad))
        check(False, "another version is refused")
    except C.CompanionError:
        check(True, "another version is refused")
    expect("proj.3", -1)

    # cut short, then whole
    c.write(3, p1, stop_after=1024)
    expect("proj.4", -1)
    c.write(3, p1)
    check(c.files()[3]["size"] == len(p1), "P4 in whole after the put cut short")

    # busy while the tape plays
    tap(PLAY)
    run(300)
    try:
        c.write(2, p1)
        check(False, "a put while the tape plays is refused")
    except C.CompanionError as e:
        check(e.code == "busy", "a put while the tape plays is refused as busy")
    tap(PLAY)
    run(300)
    c.write(2, p1)
    check(c.files()[2]["size"] == len(p1), "and goes in once it is stopped")

    # delete
    c.delete(1)
    expect("proj.2", -1)
    expect("proj.1", 0)
    check([f["size"] for f in c.files()] == [len(p1), 0, len(p1), len(p1)], "P2 deleted, the rest kept")
    # (a restart after a delete: tests/projects.txt's "proj rm", the same hook; --serve has no reboot)

    # a panel save while a computer's write holds the last free slot: the panel wins, the write is given up (sent
    # again, it goes in), and nothing outside the projects' slots is touched (the tape's ring at 0xC8000)
    panel_save(3)
    check(all(f["size"] for f in c.files()), "four projects saved")
    expect("data.0xC8000", 0x41)
    c.write(0, p1, stop_after=1024)
    panel_save(5)
    expect("toast", "P3_SAVED")
    expect("data.0xC8000", 0x41)
    expect("proj_same", 3)
    try:                                         # (the companion resumes where it was: A22 refuses the frame,
        c.write(0, p1)                           #  and the companion then forgets that write)
        check(False, "the given-up write, resumed, is refused")
    except C.CompanionError:
        check(True, "the given-up write, resumed, is refused")
    c.write(0, p1)
    check(c.read(0)[256:] == p1[256:], "and sent again from the start it goes in")

    link.close()
    print(f"projfiles: {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
