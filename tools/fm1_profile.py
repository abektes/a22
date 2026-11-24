#!/usr/bin/env python3
"""Times A22's audio on a real FM-1, with an eight-note chord held on each algorithm of an engine, and fails
if any of them runs past a limit. The device side of the plan's rule ("kept only if eight-note chords stay
under about 80% peak on the FM-1", docs/PLAN-expansion-sprints.md). Was profile.py in docs/notes/HANDOFF-perf-audio.md.

    tools/fm1_profile.py ENGINE [--fx FX] [--knob K=V ...] [--max PCT] [--secs S]
    tools/fm1_profile.py idle
    tools/fm1_profile.py jam --clear-tape [--secs S]

  ENGINE      an engine's name as the screen says it (PERC, SKINS, FM-4 ...), or "all" for every engine
  --fx FX     the effect to have on (PLATE, DELAY, ... THUMP); else the one in hand
  --knob K=V  set knob K (1..4) to V (0..100) over MIDI (CC 20-23) on every algorithm, e.g. --knob 4=100.
              A knob set so is saved, and what it was cannot be read back: after each algorithm is timed,
              SAVE is held a second (the algorithm's knobs back to their factory settings)
  --max PCT   the limit on any algorithm's peak AUDIO (default 80)
  idle        each effect with nothing playing
  jam         a dense loop jam, after a tester's on r/MVaveFM1 that stuttered on 0.4.0 at four layers, with PLATE:
              the tape cleared (CLEAR ALL: what is on the FM-1's tape is lost, hence --clear-tape), then a bar of
              SHAPER CZ-SAW four-note chords on T1, ELECTRIC FELT three notes a step on T2, KIT WORLD three drums
              a step on T3, overdubs of SHAPER GLASSY on T1 and ELECTRIC TAPE on T2 (every layer on every
              sixteenth), a STACK CHORD layer on T4, then four keys held live on T4, timed after each. The
              PERF line's LATE (halves the render missed) and SHED (voices let go to catch up) are printed with
              it; the tracks keep the jam's engines afterwards

CC 119 makes the firmware say a PERF line through the screen reader (app/app.c say_perf): FPS, DRAW ms and
its max, AUDIO % of the block's time, average and max, since the CC before. Run from the repository's top
with a Python that has mido and python-rtmidi. It leaves the FM-1 on the engine, algorithm and effect it
found, every key up and MASTER on its pot.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "tools"))
import fm1_remote as R  # noqa: E402

FXK = {"MIX. DECAY. DAMP. MOD": "PLATE", "MIX. TIME. FEEDBK. TAPE": "DELAY", "LPF. HPF. RESO. ENV": "FILTER",
       "FREQ. DELAY. FEEDBK. MIX": "SHIFT", "MIX. RATE. DEPTH. WIDTH": "CHORUS", "MIX. DECAY. TWANG. TONE": "TANK",
       "CRUSH. RATE. BAND. GRIT": "LOFI", "PUNCH. DRIVE. BASS. TONE": "THUMP"}
CHORD = [0, 2, 4, 7, 9, 11, 14, 16]
PERF = re.compile(r"FPS (\d+)\. DRAW (\d+) MAX (\d+).*AUDIO (\d+) MAX (\d+)(?: LATE (\d+) SHED (\d+))?")


class Unit:
    def __init__(self):
        self.fm = R.FM1()

    def said(self, secs):
        return [f[2:].decode("latin1", "replace") for f in self.fm.frames(R.SR, secs) if f and f[0] == 16]

    def hello(self):                             # (the screen reader's session lapses in seconds: renew it)
        self.fm.sysex(R.SR + [1, 1])
        time.sleep(0.15)
        for _ in self.fm.inp.iter_pending():
            pass

    def screen(self):
        self.hello()
        self.fm.sysex(R.SR + [3])
        return self.said(0.8)

    def where(self):
        for s in self.screen():
            if s.startswith("A22. "):
                return s[5:]
        return "?"

    def cc(self, n, v):
        self.fm.out.send(self.fm.mido.Message("control_change", control=n, value=v))

    def perf(self, secs):
        """(fps, draw, drawmax, audio%, audiomax%) over secs"""
        self.hello()
        self.cc(119, 127)
        self.said(0.4)
        time.sleep(max(0.0, secs - 0.4))
        self.hello()
        self.cc(119, 127)
        for s in self.said(1.2):
            m = PERF.search(s)
            if m:
                return tuple(int(x) if x is not None else 0 for x in m.groups())
        return None

    def run(self, *args):
        self.fm.run([str(a) for a in args])

    def fx_now(self):
        self.run("tap", "FX", "wait", 300)
        found = None
        for s in self.screen():
            for k, v in FXK.items():
                if k in s:
                    found = v
        self.run("tap", "HOME", "wait", 200)
        return found

    def fx_select(self, want):
        for _ in range(len(FXK) + 1):
            if self.fx_now() == want:
                return True
            self.run("tap", "FX", "wait", 250, "turn", "ALGO", 1, "wait", 250, "tap", "HOME", "wait", 200)
        return False

    def engine_select(self, want):
        for _ in range(16):
            if self.where().split(". ")[0] == want:
                return True
            self.run("turn", "PRESET", 1, "wait", 400)
        return False


def algo_select(u, engine, algo):
    for _ in range(9):
        if u.where() == f"{engine}. {algo}":
            return True
        u.run("turn", "ALGO", 1, "wait", 350)
    return False


def chord(u, ks, down):
    """keys struck or let go together: one message for all of them (the remote's key command waits 30 ms a key)"""
    m = 0
    for k in ks:
        m |= 1 << k
    u.fm.keys = (u.fm.keys | m) if down else (u.fm.keys & ~m)
    u.fm.keys_send()


def take(u, steps, step_ms=170, hold_ms=150):
    """a take on the track in hand: REC, then steps (each a list of keys struck together, held hold_ms, one every
    step_ms), twice round, then REC. An empty step is a rest"""
    u.run("tap", "REC", "wait", 200)
    for ks in steps + steps:
        t0 = time.monotonic()
        if ks:
            chord(u, ks, 1)
        time.sleep(max(0.0, hold_ms / 1000 - (time.monotonic() - t0)))
        if ks:
            chord(u, ks, 0)
        time.sleep(max(0.0, step_ms / 1000 - (time.monotonic() - t0)))
    u.run("tap", "REC", "wait", 600)


# The jam, as full as it gets: 16 steps of 170 ms (sixteenths at 88 BPM), every layer on every step, so the
# eight voices are always taken and stolen
STAB = [[0, 4, 7, 11] if s % 2 == 0 else [2, 5, 9, 12] for s in range(16)]               # SHAPER: four-note chords
ARP = [[14 + (0, 4, 7, 11)[s % 4], 17 + (0, 3, 7)[s % 3], 24] for s in range(16)]        # ELECTRIC: three notes a step
BEAT = [[7 if s % 2 == 0 else 9, 13, (11, 12, 15, 17)[s % 4]] for s in range(16)]       # KIT: three drums a step
STAB2 = [[3, 10, 14, 19] if s % 2 else [1, 8, 13, 20] for s in range(16)]                # SHAPER GLASSY on T1, over it
ARP2 = [[16 + (0, 2, 5, 9)[s % 4], 21, 25] for s in range(16)]                           # ELECTRIC TAPE on T2, over it
PADS = [[5, 12, 16, 19] if s % 4 < 2 else [7, 14, 18, 21] for s in range(16)]          # STACK CHORD on T4: chords
HOLD4 = [22, 23, 25, 26]                                                                 # and four keys live on top


def jam(u, secs):
    JAM = [("T1 SHAPER CZ-SAW, 4-note chords", "SHAPER", "CZ-SAW", STAB, 0),
           ("+ T2 ELECTRIC FELT, 3 notes a step", "ELECTRIC", "FELT", ARP, 1),
           ("+ T3 KIT WORLD, 3 drums a step", "KIT", "WORLD", BEAT, 1),
           ("+ T1 overdub, SHAPER GLASSY", "SHAPER", "GLASSY", STAB2, -2),
           ("+ T2 overdub, ELECTRIC TAPE", "ELECTRIC", "TAPE", ARP2, 1),
           ("+ T4 STACK CHORD, 4-note chords", "STACK", "CHORD", PADS, 2)]
    u.run("release-all", "tap", "HOME", "wait", 300)
    if not u.fx_select("PLATE"):
        print("no PLATE")
        return [], False
    u.run("press", "REC", "wait", 3400, "release", "REC", "wait", 400, "tap", "REC", "wait", 800)   # CLEAR ALL
    rows = []
    for name, eng, alg, steps, move in JAM:
        if move:
            u.run("press", "EDIT", "turn", "PRESET", move, "release", "EDIT", "wait", 500)   # (tracks wrap)
        if not (u.engine_select(eng) and algo_select(u, eng, alg)):
            print(f"no {eng} {alg}")
            return rows, False
        take(u, steps)
        time.sleep(0.5)
        rows.append((name, u.perf(secs)))
        print(f"  {name:36s} {rows[-1][1]}", flush=True)
    chord(u, HOLD4, 1)                                                                      # (on T4, live)
    time.sleep(0.3)
    rows.append(("+ 4 keys held live on T4", u.perf(secs)))
    chord(u, HOLD4, 0)
    return rows, True


def knob_cc(v):
    return max(0, min(127, (int(v) * 127 + 50) // 100))


def time_engine(u, knobs, secs):
    """every algorithm of the engine in hand: [(where, perf)]"""
    rows = []
    for _ in range(8):
        w = u.where()
        for k, v in knobs:
            u.cc(19 + k, knob_cc(v))
        time.sleep(0.2)
        for key in CHORD:
            u.run("key", key, "down")
        time.sleep(0.3)
        p = u.perf(secs)
        for key in CHORD:
            u.run("key", key, "up")
        if knobs:                                    # (SAVE held 0.9..2.2 s: this algorithm's knobs to factory)
            u.run("press", "SAVE", "wait", 1300, "release", "SAVE", "wait", 300)
        rows.append((w, p))
        print(f"  {w:24s} {'FPS %d  DRAW %d max %d ms  AUDIO %d%% max %d%%' % p[:5] if p else 'no PERF line'}", flush=True)
        time.sleep(0.4)
        u.run("turn", "ALGO", 1, "wait", 350)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("engine")
    ap.add_argument("--fx")
    ap.add_argument("--knob", action="append", default=[])
    ap.add_argument("--max", type=int, default=80)
    ap.add_argument("--secs", type=float, default=2.2)
    ap.add_argument("--clear-tape", action="store_true")
    a = ap.parse_args()
    knobs = [(int(k), int(v)) for k, v in (s.split("=") for s in a.knob)]

    u = Unit()
    u.run("release-all", "tap", "HOME", "wait", 300)
    start, fx0 = u.where(), u.fx_now()
    print(f"found {start}, effect {fx0}", flush=True)
    bad = []
    try:
        if a.engine == "jam":
            if not a.clear_tape:
                print("jam clears the FM-1's tape (CLEAR ALL): say --clear-tape")
                return 2
            rows, ok = jam(u, max(a.secs, 4.0))
            for w, p in rows:
                print(f"  {w:36s} " + ("FPS %d  DRAW %d max %d ms  AUDIO %d%% max %d%%  LATE %d  SHED %d" % p if p else "no PERF line"),
                      flush=True)
                if p is None or p[4] > a.max or p[5]:
                    bad.append((w, p))
            start = "?"                               # (the tracks keep the jam's engines: nothing to put back)
            if not ok:
                return 2
        elif a.engine == "idle":
            for fx in FXK.values():
                u.fx_select(fx)
                time.sleep(0.5)
                print(f"  idle {fx:7s}", u.perf(2.5), flush=True)
            return 0
        elif a.fx and not u.fx_select(a.fx):
            print(f"no effect {a.fx}")
            return 2
        engines = []
        if a.engine == "all":                        # (round the engines once: a turn that does not land,
            for _ in range(24):                       # or a read that comes back "?", is not counted twice)
                e = u.where().split(". ")[0]
                if e != "?" and e not in engines:
                    engines.append(e)
                elif e == (engines[0] if engines else None) and len(engines) >= 14:
                    break
                u.run("turn", "PRESET", 1, "wait", 400)
        elif a.engine != "jam":
            engines = [a.engine]
        for e in engines:
            if not u.engine_select(e):
                print(f"no engine {e}")
                return 2
            print(f"{e} with {fx0 if not a.fx else a.fx}{', knobs ' + ' '.join(a.knob) if knobs else ''}:", flush=True)
            rows = time_engine(u, knobs, a.secs)
            for w, p in rows:
                if p is None or p[4] > a.max:
                    bad.append((w, p))
            avg = [p[3] for _, p in rows if p]
            mx = [p[4] for _, p in rows if p]
            if avg:
                print(f"  {e}: average {sum(avg) / len(avg):.0f}%, worst average {max(avg)}%, peak {max(mx)}%", flush=True)
    finally:
        u.run("release-all")
        if (a.fx or a.engine == "idle") and fx0:   # (idle goes round every effect)
            u.fx_select(fx0)
        if start != "?":
            u.engine_select(start.split(". ")[0])
            for _ in range(8):
                if u.where() == start:
                    break
                u.run("turn", "ALGO", 1, "wait", 300)
        u.run("tap", "HOME", "master", "pot")
    if bad:
        print(f"FAIL: past {a.max}%:", ", ".join(f"{w} ({p[4] if p else '?'}%)" for w, p in bad))
        return 1
    print(f"PASS: every algorithm under {a.max}% peak")
    return 0


if __name__ == "__main__":
    rc = main()
    sys.stdout.flush()
    os._exit(rc)                                 # (a MIDI port's thread could keep the process from ending)
