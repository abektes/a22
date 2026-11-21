#!/usr/bin/env python3
"""Turns build/a22-core.wasm into dist/a22.rfm1: adds the custom sections the .rfm1 format asks for
(the "rfm1" manifest, "rfm1.credits", "rfm1.license.0") and checks the module first: its only
imports may be WASI's, its memory fixed in size, and every required export present.

  tools/pack_rfm1.py [--version X.Y] [--commit SHA]"""
import argparse, json, os, subprocess, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
REQUIRED = ["memory", "rfm1_version", "rfm1_get", "rfm1_layout", "rfm1_alloc", "rfm1_free", "rfm1_options",
            "rfm1_boot_options", "rfm1_init", "rfm1_tick", "rfm1_frame", "rfm1_idle", "rfm1_audio", "rfm1_midi_in",
            "rfm1_midi_out_take", "rfm1_halted", "rfm1_shutdown", "rfm1_flash_stage", "rfm1_flash",
            "rfm1_flash_dirty", "rfm1_flash_sync", "rfm1_stats", "rfm1_ui_info"]


def uleb(v):
    out = bytearray()
    while True:
        c = v & 0x7F
        v >>= 7
        out.append(c | (0x80 if v else 0))
        if not v:
            return bytes(out)


def leb(b, i):
    v = s = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return v, i


def name(b, i):
    n, i = leb(b, i)
    return b[i:i + n].decode(), i + n


def check(b):
    assert b[:8] == b"\0asm\x01\0\0\0", "not a wasm module"
    i, imports, exports, mem = 8, [], [], None
    while i < len(b):
        sid, i = b[i], i + 1
        size, i = leb(b, i)
        end = i + size
        if sid == 2:
            n, j = leb(b, i)
            for _ in range(n):
                mod, j = name(b, j)
                fld, j = name(b, j)
                kind = b[j]
                j += 1
                imports.append((mod, fld, kind))
                if kind == 0: _, j = leb(b, j)
                elif kind == 1: j += 1; f = b[j]; j += 1; _, j = leb(b, j); j = leb(b, j)[1] if f & 1 else j
                elif kind == 2: f = b[j]; j += 1; _, j = leb(b, j); j = leb(b, j)[1] if f & 1 else j
                elif kind == 3: j += 2
        elif sid == 5:
            n, j = leb(b, i)
            f = b[j]; j += 1
            lo, j = leb(b, j)
            hi = leb(b, j)[0] if f & 1 else None
            mem = (lo, hi)
        elif sid == 7:
            n, j = leb(b, i)
            for _ in range(n):
                nm, j = name(b, j)
                j += 1
                _, j = leb(b, j)
                exports.append(nm)
        i = end
    bad = [m for m in imports if m[0] != "wasi_snapshot_preview1"]
    assert not bad, "imports outside WASI: %r" % bad
    assert mem and mem[1] == mem[0], "the memory must be fixed in size (initial = maximum): %r" % (mem,)
    missing = [e for e in REQUIRED if e not in exports]
    assert not missing, "missing exports: %r" % missing
    return mem[0] * 65536


def section(nm, payload):
    body = uleb(len(nm.encode())) + nm.encode() + payload
    return b"\0" + uleb(len(body)) + body


ap = argparse.ArgumentParser()
ap.add_argument("--version", default="dev")
ap.add_argument("--commit", default=None)
a = ap.parse_args()
commit = a.commit
if not commit:
    try:
        commit = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    except OSError:
        commit = ""
wasm = open(os.path.join(root, "build/a22-core.wasm"), "rb").read()
mem = check(wasm)
manifest = {"rfm1": 1, "abi": 6, "id": "a22", "name": "A22", "version": a.version,
            "license": "MIT",
            "source_url": "https://github.com/abektes/a22" + ("@" + commit if commit else ""),
            "authors": ["A22", "Jake Handy"], "built_by": "A22 tools/build_rfm1.sh, clang wasm32",
            "memory_mib": mem // (1024 * 1024)}
out = wasm + section("rfm1", json.dumps(manifest, indent=1).encode())
out += section("rfm1.credits", "A22 FM workstation for the M-VAVE FM-1 (derived from h1 by Jake Handy).\n".encode())
out += section("rfm1.license.0", ("MIT\n" + open(os.path.join(root, "LICENSE"), encoding="utf-8").read()).encode())
os.makedirs(os.path.join(root, "dist"), exist_ok=True)
open(os.path.join(root, "dist/a22.rfm1"), "wb").write(out)
print("wrote dist/a22.rfm1, %d KB (%d MiB memory)" % (len(out) // 1024, mem >> 20))
