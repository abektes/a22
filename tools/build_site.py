#!/usr/bin/env python3
"""Builds A22's website into a folder: the guide (index.html), the engines' pictures and the browser simulator.

    python3 tools/build_site.py OUT

.github/workflows/pages.yml runs it and publishes OUT to GitHub Pages (https://abektes.github.io/a22/). The
simulator is web/ with dist/a22.rfm1 beside it, as the repository lays them out (it loads ../dist/a22.rfm1).
"""
import html
import os
import shutil
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
REPO = "https://github.com/abektes/a22"
RELEASE = REPO + "/releases/latest"
COFFEE = "https://buymeacoffee.com/abektes"
HUB = "https://thefirmwarehub.com"

KNOB_COLORS = ["var(--k1)", "var(--k2)", "var(--k3)", "var(--k4)"]

# name, ink, picture, what it is, [(knob, what it does) x4], [(algorithm, what it is) x8]
ENGINES = [
    ("FM-4", (59, 158, 255), "fm4", "Four-operator stereo FM, oversampled, with a warm resonant filter after it.",
     [("RATIO", "the modulators' ratios, from harmonic to clangorous"),
      ("DEPTH", "how hard they modulate, with the envelope and velocity"),
      ("FEEDBK", "operator 4 feeding itself, and the carriers spread in stereo"),
      ("FILTER", "a resonant low-pass on every voice")],
     [("KEYS", "two FM pairs side by side: electric piano"), ("BASS", "four operators in a stack, a sub under it"),
      ("BELL", "a branching stack with a fast chime on top"), ("PAD", "detuned carriers, slow and wide"),
      ("BRASS", "two horn pairs that swell in"), ("PLUCK", "three modulators into one carrier, struck"),
      ("ORGAN", "four harmonics side by side, a little cross-FM"), ("ALIEN", "operators feeding each other, ring-FM")]),
    ("ELECTRIC", (68, 227, 126), "electric", "Electric pianos and keys, with stereo tremolo and a rotary.",
     [("BARK", "the overtone drive that bites when you dig in"), ("TINE", "the metal strike, bell sparkle and hammer"),
      ("TREM", "stereo tremolo, or the rotary's depth"), ("WARMTH", "cabinet warmth, felt, tape age")],
     [("RHODES", "a suitcase tine piano"), ("WURLI", "a reed piano that barks"), ("DX-EP", "the crystal FM piano"),
      ("CLAV", "bright and funky, plucked"), ("ORGAN", "tonewheel drawbars"), ("FELT", "a felted upright"),
      ("VIBES", "a vibraphone bar and its fan"), ("TAPE", "the tine piano off a worn tape")]),
    ("STACK", (76, 224, 122), "stack", "Stacked chords and wide pads: four oscillators a voice, drifting.",
     [("SPREAD", "detune and stereo width, mono to widescreen"), ("CHORD", "the stack: unison, fifth, sevenths, sus, ninths"),
      ("DRIFT", "tape wander and FM shimmer"), ("GLOW", "a warm resonant filter and its bloom")],
     [("CLOUD", "sine and triangle with FM shimmer"), ("CHORD", "four-note saw and sine chords"),
      ("JUNO", "detuned saws, PWM and a sub"), ("GLASS", "crystalline FM overtones"),
      ("ORGAN", "stacked drawbars with a click"), ("DRONE", "a deep, slowly moving monolith"),
      ("CHOIR", "a stack through vowel formants"), ("DREAM", "lo-fi chords on a wobbling tape")]),
    ("ANALOG", (238, 241, 245), "analog", "Virtual analog with thru-zero FM and a 24 dB ladder filter.",
     [("SHAPE", "pulse width, saw to pulse, the chord or the sync sweep"), ("TZFM", "thru-zero FM depth, or unison detune"),
      ("CUTOFF", "the ladder filter, following envelope and velocity"), ("RESO", "resonance and a warm drive before it")],
     [("DUAL", "a detuned saw and pulse in stereo"), ("TZFM", "thru-zero FM that stays in tune"),
      ("SUPER", "five oscillators: supersaw"), ("SUB", "a fat bass on a clean sub"),
      ("SYNC", "a hard-synced saw, swept"), ("ACID", "saw to square into a screaming ladder"),
      ("BRASS", "a pitch scoop into every note"), ("CHORD", "a chord stack in stereo")]),
    ("SHAPER", (42, 230, 224), "shaper", "Phase distortion in the style of the CZ, through a West Coast wavefolder.",
     [("WARP", "the phase kink, resonant peak or digital warp"), ("FOLD", "how hard the wavefolder is driven"),
      ("BIAS", "the folder leans: even against odd harmonics"), ("TONE", "a resonant low-pass on every voice")],
     [("CZ-SAW", "a phase-distorted saw, folded"), ("CZ-RES", "the CZ's resonant peak"),
      ("BUCHLA", "a complex oscillator: FM and two folds"), ("PLUCK", "a crisp digital mallet"),
      ("METRIC", "metallic, inharmonic FM"), ("CYBER", "a folded lead an octave over a sub"),
      ("GLASSY", "a delicate double-pulse chime"), ("CRUSH", "a stepped, quantized fold")]),
    ("WIRE", (255, 210, 74), "wire", "Plucked and bowed strings, physically modelled: a string, a pick or bow, a body.",
     [("DAMP", "how soon the string dies: muted nylon to ringing steel"), ("PLUCK", "where and how hard it is picked"),
      ("BODY", "the wooden soundboard and the pickups' width"), ("TENSION", "stiffness, a twelve-string shimmer, sitar buzz")],
     [("NYLON", "a classical guitar"), ("STEEL", "a steel-string guitar"), ("HARP", "long, open strings"),
      ("KOTO", "a pluck that bends as it lets go"), ("CLAV", "a short, bright pluck"), ("BASS", "a deep string"),
      ("SITAR", "a buzzing bridge"), ("BOWED", "a bow that keeps it sounding while you hold")]),
    ("HAZE", (155, 118, 255), "haze", "Evolving ambient textures: FM pairs through moving vowel formants, with a shimmer on top.",
     [("MORPH", "hollow and pure to rich, moving FM"), ("FORMANT", "the two formants sweep OO, OH, AH, EH, EE"),
      ("DRIFT", "tape wow and flutter, the stereo drifting"), ("SHIMMER", "a crystalline octave above")],
     [("AETHER", ""), ("VOX", ""), ("SOLAR", ""), ("ICE", ""), ("MIST", ""), ("ABYSS", ""), ("PRISM", ""), ("GHOST", "")]),
    ("MONO", (255, 107, 26), "mono", "Monophonic leads and basses, legato, with glide and a ladder filter.",
     [("FM-BITE", "FM punch and a snarling edge"), ("CUTOFF", "the ladder filter"),
      ("GLIDE", "slide time, snappy to a liquid 303 slide"), ("SUB-DRV", "sub weight, resonance and drive")],
     [("ACID", "the 303 with an FM edge"), ("REESE", "wide beating saws on a tight sub"),
      ("SH-LEAD", "a warm saw, PWM and sub lead"), ("ZAP-BASS", "a punchy FM club bass"),
      ("GROWL", "a high-feedback FM growl"), ("SUB-808", "a pure, booming sub"),
      ("LASER", "hard sync and an FM sweep"), ("HOOVER", "the rave one: detuned PWM, a swoop up")]),
    ("KIT", (255, 107, 26), "kit", "Drum kits: twelve drums on every octave, synthesised, some layered with CC0 recordings.",
     [("TUNE", "the whole kit up or down, an octave each way"), ("DECAY", "tight to booming"),
      ("PUNCH", "pitch drop, click and compression"), ("FM-GRIT", "FM, saturation and crunch")],
     [("808", "the classic boom"), ("909", "punchy, with recorded transients"), ("CYCLES", "all FM"),
      ("CRISP", "synthesis with recorded hits layered in"), ("LOFI", "crunchy and dusty"),
      ("MODULAR", "patch-cable drums"), ("METAL", "clangy metallic clusters"), ("WORLD", "hand drums and bells")]),
    ("PERC", (255, 194, 82), "perc", "Tuned percussion from resonating modes, playable on every key.",
     [("PITCH", "the overtones stretched: wood, pure, bell"), ("MALLET", "soft felt to hard brass"),
      ("RING", "how long it rings"), ("SPACE", "stereo spread and the cavity's body")],
     [("HANDPAN", "a handpan"), ("MARIMBA", "a marimba"), ("KALIMBA", "a kalimba"), ("TABLA", "a tabla"),
      ("WOOD", "woodblocks"), ("GAMELAN", "a gamelan bar"), ("STEEL", "a steel pan"), ("GLITCH", "micro-bursts, IDM")]),
    ("WAVE", (59, 158, 255), "wave", "Wavetables: eight banks of fifteen waves, morphing, band-limited per octave.",
     [("WAVE", "where in the bank, morphing wave to wave"), ("SCAN", "how far a note moves up the bank as it dies"),
      ("SPREAD", "the right side detuned and further along"), ("FILTER", "a resonant low-pass")],
     [("HARMON", "a sine growing into a saw"), ("ORGAN", "drawbar registrations"), ("VOX", "the vowels"),
      ("DIGI", "bands of upper harmonics"), ("PULSE", "a square narrowing to a thin pulse"),
      ("SYNC", "a hard-synced saw, tearing"), ("GLASS", "sparse partials, bells"), ("GROWL", "a comb closing in")]),
    ("CHIP", (68, 227, 126), "chip", "8-bit console voices: pulses, a stepped triangle, noise, arpeggios.",
     [("DUTY", "pulse width in sixteenths; long or short noise"), ("SWEEP", "a pitch drop into each note"),
      ("ARP", "arpeggio rate, vibrato or the noise's pitch"), ("CRUSH", "fewer bits, a lower sample rate")],
     [("PULSE", "a square, width on DUTY"), ("TRI", "the sixteen-step triangle"), ("NOISE", "the noise register"),
      ("ARP", "a fast chord, a note at a time"), ("BASS", "a pulse over a triangle, low"),
      ("LEAD", "a duty that sways, delayed vibrato"), ("DRUM", "a falling triangle and a noise burst"),
      ("DUET", "two pulses a fifth apart")]),
]

EFFECTS = [("PLATE", "plate reverb", "MIX, DECAY, DAMP, MOD"), ("DELAY", "tape ping-pong", "MIX, TIME, FEEDBK, TAPE"),
           ("FILTER", "resonant filter with an envelope follower", "LPF, HPF, RESO, ENV"),
           ("SHIFT", "frequency shifter and comb", "FREQ, DELAY, FEEDBK, MIX"), ("CHORUS", "chorus", "MIX, RATE, DEPTH, WIDTH"),
           ("TANK", "spring reverb", "MIX, DECAY, TWANG, TONE"), ("LOFI", "lo-fi", "CRUSH, RATE, BAND, GRIT"),
           ("THUMP", "drive and transient", "PUNCH, DRIVE, BASS, TONE")]

PAGES = [("FX", "FX", "the effect's four knobs"),
         ("MIX", "SEL", "volume, pan and FX send per track; the master's LOW, MID, HIGH and DRIVE"),
         ("ENV", "ENV", "ATTACK, DECAY, SUSTAIN, RELEASE; the mod envelope's DEPTH, DECAY, VEL, PUNCH"),
         ("LFO", "LFO", "SHAPE, RATE, DEPTH, TO (pitch, a knob, level or pan)"),
         ("GLOBAL", "GLO", "TEMPO, SWING, TUNE, BRIGHT; KEYS: ROOT, SCALE, GLIDE, HOLD; CHORD: MODE, TYPE, VOICING, STRUM"),
         ("SEQ", "ARP", "MODE (OFF, RIFF, SHAKE, UP, DOWN, UP/DOWN, RANDOM, CHORD), RATE, RANGE, GATE"),
         ("TAPE", "SEQ", "TRACK, BARS (1 to 32), SPEED (0.5X, 1X, 2X, REV), TRICK; edit: IN BAR, OUT BAR, SNAP, PITCH")]

HOTKEYS = [("PRESETS", "The engine. With EDIT held: the track in hand, T1 to T4."),
           ("ALGORITHM", "One of the engine's eight algorithms. On the FX page the effect, on the SEQ page (or with EDIT held) the sequencer mode."),
           ("KNOB 1–4", "Home: the engine's four knobs. On a page: its four columns."),
           ("SELECT", "Home: tempo. On a page: the next subpage. With EDIT held: the track's volume."),
           ("MASTER", "Volume of everything. With EDIT held: the track's volume."),
           ("FX SEL ENV LFO GLO ARP SEQ", "Open that page. HOME goes back."),
           ("EDIT", "Pins the knob strip. Writing steps or a riff: a rest."),
           ("EDIT + FX", "Effect on or bypassed."), ("EDIT + SEL", "Mute the track in hand."),
           ("EDIT + ARP", "Hold (latch) on and off."),
           ("EDIT + GLO", "With keys held: store them as the chord. With none: chords off, or on again."),
           ("EDIT + REC / PLAY", "Cut the track's notes to a clipboard / paste them at the current bar."),
           ("SAVE + white key", "Load that snapshot, or save to it if it is empty. SAVE + EDIT + key saves over it."),
           ("hold SAVE", "Reset the algorithm's knobs. Two seconds: the engine. Four: every engine and the GLOBAL and KEYS pages."),
           ("hold REC", "Clear the track in hand. Keep holding and it asks CLEAR ALL?: tap REC to clear the whole tape."),
           ("hold SEQ", "Swap the tape and the step sequencer."), ("hold GLO", "Metronome: CLICK REC, CLICK ON, CLICK OFF."),
           ("hold HOME", "Silence everything (and send CC 123)."),
           ("OCT − / +", "Octave, two each way. Both together: the middle octave."),
           ("hold OCT − and OCT +", "USB host for a keyboard, or back to the computer.")]

FAQ = [("Will it brick my FM-1?",
        "The chip's bootloader is in ROM and can't be overwritten, and A22's package carries fm1-base's update loader, so other installers can always replace it. If an install stops halfway, plug in and install again: it picks up where it stopped. If A22 fails to start twice it comes up in SAFE MODE with USB working. And holding REC while you switch on enters the chip's own update mode."),
       ("Which FM-1s does it run on?",
        "The M-VAVE FM-1, with either flash chip: the original Puya P25Q80 and the UC25HQ80 in units sold since autumn 2026. Not the SMK-37."),
       ("How do I go back to stock?",
        "Press Back to official V15 in The Firmware Hub's Official firmware box, or install another custom firmware with its own installer. A22 only saves its own settings."),
       ("Do my settings survive an update?",
        "Yes, unless a release changes how settings are stored; its notes say so. Your recorded tape is kept either way."),
       ("Can I record it into my computer?",
        "Yes. Over one USB cable the FM-1 is an audio input (the whole mix at the MASTER volume) and a MIDI port: notes in at their velocity, CC 20 to 23 for the knobs, and the keys, arpeggiator and tape out on channel 1."),
       ("Is it related to h1?",
        "A22 is derived from h1 by Jake Handy, with its own engines, a four-track tape, effects, a mixer and colour pictures. It is built on fm1-base by Charles Vestal and felucca-bsp from Felucca by Leo Kuroshita.")]


def esc(s):
    return html.escape(s, quote=True)


def rgb(c):
    return "rgb(%d %d %d)" % c


def engine_cards():
    out = []
    for name, ink, pic, what, knobs, algos in ENGINES:
        kn = "".join(
            f'<li><b style="--c:{KNOB_COLORS[i]}">{esc(k)}</b><span>{esc(d)}</span></li>' for i, (k, d) in enumerate(knobs))
        al = "".join(f"<li><b>{esc(a)}</b>{(' <span>' + esc(d) + '</span>') if d else ''}</li>" for a, d in algos)
        out.append(f"""
      <article class="engine" id="engine-{esc(pic)}" style="--ink:{rgb(ink)}">
        <img src="img/{esc(pic)}.png" width="240" height="240" alt="{esc(name)}'s picture on the FM-1's screen" loading="lazy">
        <div class="engine-body">
          <h3>{esc(name)}</h3>
          <p>{esc(what)}</p>
          <h4>Knobs</h4>
          <ul class="knobs">{kn}</ul>
          <h4>Algorithms</h4>
          <ul class="algos">{al}</ul>
        </div>
      </article>""")
    return "".join(out)


def mosaic():
    return "".join(f'<img src="img/{esc(p)}.png" width="240" height="240" alt="" style="--ink:{rgb(c)}">'
                   for _, c, p, *_ in ENGINES)


def chips():
    return "".join(f'<a href="#engine-{esc(p)}" style="--ink:{rgb(c)}">{esc(n)}</a>' for n, c, p, *_ in ENGINES)


def page():
    effects = "".join(f"<tr><th>{esc(n)}</th><td>{esc(d)}</td><td>{esc(k)}</td></tr>" for n, d, k in EFFECTS)
    pages = "".join(f"<tr><th>{esc(n)}</th><td><kbd>{esc(b)}</kbd></td><td>{esc(c)}</td></tr>" for n, b, c in PAGES)
    hot = "".join(f"<div><dt>{esc(k)}</dt><dd>{esc(v)}</dd></div>" for k, v in HOTKEYS)
    faq = "".join(f"<details><summary>{esc(q)}</summary><p>{esc(a)}</p></details>" for q, a in FAQ)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>A22: an FM workstation for the M-VAVE FM-1</title>
<meta name="description" content="A22 is free, open-source custom firmware for the M-VAVE FM-1: twelve synth and drum engines in colour, eight effects, a four-track tape, a mixer, Riff and Shake sequencers, USB audio and MIDI.">
<meta property="og:title" content="A22: an FM workstation for the M-VAVE FM-1">
<meta property="og:description" content="Twelve engines, eight effects, a four-track tape. Free custom firmware for the FM-1.">
<meta property="og:image" content="img/fm4.png">
<link rel="icon" href="img/fm4.png">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=Space+Grotesk:wght@500;700&display=swap" rel="stylesheet">
<style>
:root {{
  color-scheme: dark;
  --bg: #0a0b0e; --panel: #12141a; --panel-2: #181b22; --line: #262a33;
  --text: #e9ecf1; --muted: #8d95a5;
  --k1: #3b9eff; --k2: #2cc96b; --k3: #dfe4ec; --k4: #ff6b1a;
  --coffee: #ffdd00;
  --mono: "JetBrains Mono", ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --display: "Space Grotesk", system-ui, sans-serif;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--bg); color: var(--text); font: 15px/1.6 var(--mono); }}
a {{ color: var(--k1); }}
a:hover {{ color: #7cc0ff; }}
img {{ image-rendering: pixelated; }}
.wrap {{ width: min(1120px, 100% - 32px); margin-inline: auto; }}
header.top {{ position: sticky; top: 0; z-index: 5; background: rgb(10 11 14 / .86); backdrop-filter: blur(8px);
  border-bottom: 1px solid var(--line); }}
header.top .wrap {{ display: flex; align-items: center; gap: 20px; height: 56px; }}
.logo {{ font: 700 20px var(--display); color: var(--text); text-decoration: none; letter-spacing: .02em; }}
.logo span {{ color: var(--k4); }}
nav {{ display: flex; gap: 18px; margin-left: auto; font-size: 13px; overflow-x: auto; }}
nav a {{ color: var(--muted); text-decoration: none; white-space: nowrap; }}
nav a:hover {{ color: var(--text); }}
.hero {{ display: grid; grid-template-columns: minmax(0, 1.05fr) minmax(0, 1fr); gap: 48px; align-items: center; padding: 72px 0 56px; }}
.hero h1 {{ font: 700 clamp(64px, 11vw, 132px)/0.9 var(--display); margin: 0 0 18px; letter-spacing: -.02em; }}
.hero h1 .dot {{ color: var(--k4); }}
.lede {{ font-size: 18px; color: var(--text); max-width: 34ch; margin: 0 0 10px; }}
.sub {{ color: var(--muted); margin: 0 0 28px; max-width: 46ch; }}
.ctas {{ display: flex; flex-wrap: wrap; gap: 10px; }}
.btn {{ display: inline-flex; align-items: center; gap: 8px; padding: 11px 16px; border-radius: 10px; font: 600 14px var(--mono);
  text-decoration: none; border: 1px solid var(--line); color: var(--text); background: var(--panel); }}
.btn:hover {{ border-color: #3a404c; color: var(--text); }}
.btn.primary {{ background: var(--k4); border-color: var(--k4); color: #120700; }}
.btn.primary:hover {{ background: #ff8a47; color: #120700; }}
.btn.coffee {{ background: var(--coffee); border-color: var(--coffee); color: #1a1500; }}
.btn.coffee:hover {{ background: #ffe84d; color: #1a1500; }}
.mosaic {{ display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 8px; }}
.mosaic img {{ width: 100%; height: auto; border-radius: 8px; background: #000; outline: 1px solid var(--line);
  transition: outline-color .2s, transform .2s; }}
.mosaic img:hover {{ outline-color: var(--ink); transform: translateY(-2px); }}
.facts {{ display: grid; grid-template-columns: repeat(5, minmax(0, 1fr)); border: 1px solid var(--line); border-radius: 14px;
  overflow: hidden; margin-bottom: 24px; }}
.facts div {{ padding: 18px 16px; background: var(--panel); border-right: 1px solid var(--line); }}
.facts div:last-child {{ border-right: 0; }}
.facts b {{ display: block; font: 700 28px var(--display); }}
.facts span {{ color: var(--muted); font-size: 13px; }}
.chips {{ display: flex; flex-wrap: wrap; gap: 8px; margin: 0 0 72px; }}
.chips a {{ font-size: 12px; font-weight: 600; text-decoration: none; color: var(--ink); border: 1px solid currentColor;
  border-radius: 999px; padding: 4px 11px; opacity: .85; }}
.chips a:hover {{ opacity: 1; background: color-mix(in srgb, var(--ink) 12%, transparent); }}
section {{ padding: 56px 0; border-top: 1px solid var(--line); }}
.label {{ display: flex; gap: 14px; align-items: baseline; margin-bottom: 22px; }}
.label .n {{ color: var(--k4); font-weight: 600; }}
.label h2 {{ font: 700 clamp(26px, 4vw, 36px)/1.1 var(--display); margin: 0; }}
section > .wrap > p, .prose p {{ max-width: 72ch; color: #cfd4dd; }}
.engines {{ display: grid; gap: 18px; }}
.engine {{ display: grid; grid-template-columns: 240px minmax(0, 1fr); gap: 24px; padding: 20px; background: var(--panel);
  border: 1px solid var(--line); border-radius: 16px; border-left: 3px solid var(--ink); scroll-margin-top: 72px; }}
.engine img {{ width: 240px; height: 240px; border-radius: 10px; background: #000; }}
.engine h3 {{ font: 700 26px var(--display); margin: 0 0 4px; color: var(--ink); }}
.engine p {{ margin: 0 0 14px; color: #cfd4dd; }}
.engine h4 {{ font-size: 11px; letter-spacing: .14em; text-transform: uppercase; color: var(--muted); margin: 14px 0 8px; }}
.knobs {{ list-style: none; padding: 0; margin: 0; display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 6px 18px; }}
.knobs li {{ display: flex; gap: 10px; font-size: 13px; }}
.knobs b {{ color: var(--c); min-width: 9ch; }}
.knobs span, .algos span {{ color: var(--muted); }}
.algos {{ list-style: none; padding: 0; margin: 0; display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 4px 18px; font-size: 13px; }}
.algos b {{ color: var(--text); margin-right: 6px; }}
table {{ width: 100%; border-collapse: collapse; font-size: 14px; }}
th, td {{ text-align: left; padding: 10px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
th {{ font-weight: 600; white-space: nowrap; }}
td {{ color: #cfd4dd; }}
kbd {{ font: 600 12px var(--mono); padding: 2px 7px; border-radius: 6px; background: var(--panel-2); border: 1px solid var(--line); }}
dl.keys {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 0 32px; margin: 0; }}
dl.keys div {{ display: grid; grid-template-columns: 15ch 1fr; gap: 14px; padding: 10px 0; border-bottom: 1px solid var(--line); }}
dt {{ font-weight: 600; color: var(--k4); }}
dd {{ margin: 0; color: #cfd4dd; font-size: 14px; }}
.cols {{ display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 24px; }}
.card {{ background: var(--panel); border: 1px solid var(--line); border-radius: 14px; padding: 20px 22px; }}
.card h3 {{ font: 700 19px var(--display); margin: 0 0 8px; }}
.card p, .card li {{ color: #cfd4dd; font-size: 14px; }}
.card ul {{ padding-left: 18px; margin: 0; }}
pre {{ background: #07080a; border: 1px solid var(--line); border-radius: 10px; padding: 14px 16px; overflow-x: auto; font-size: 13px; }}
details {{ border-bottom: 1px solid var(--line); padding: 14px 0; }}
summary {{ cursor: pointer; font-weight: 600; }}
details p {{ color: #cfd4dd; margin: 10px 0 0; max-width: 76ch; }}
.support {{ display: flex; align-items: center; justify-content: space-between; gap: 24px; flex-wrap: wrap;
  background: linear-gradient(120deg, #1b1706, var(--panel) 60%); border: 1px solid #3a3210; border-radius: 16px; padding: 24px 26px; }}
.support h3 {{ font: 700 22px var(--display); margin: 0 0 4px; }}
.support p {{ margin: 0; color: #cfd4dd; }}
footer {{ border-top: 1px solid var(--line); padding: 32px 0 56px; color: var(--muted); font-size: 13px; }}
footer p {{ margin: 6px 0; max-width: 90ch; }}
:focus-visible {{ outline: 2px solid var(--k4); outline-offset: 2px; border-radius: 4px; }}
@media (max-width: 900px) {{
  .hero {{ grid-template-columns: minmax(0, 1fr); padding-top: 48px; }}
  .facts {{ grid-template-columns: repeat(2, minmax(0, 1fr)); }}
  .facts div {{ border-bottom: 1px solid var(--line); }}
  .engine {{ grid-template-columns: minmax(0, 1fr); }}
  .engine img {{ width: 100%; max-width: 240px; height: auto; }}
  dl.keys, .cols {{ grid-template-columns: minmax(0, 1fr); }}
  nav {{ display: none; }}
}}
@media (max-width: 560px) {{
  .knobs, .algos {{ grid-template-columns: minmax(0, 1fr); }}
  .ctas .btn {{ flex: 1 1 100%; justify-content: center; }}
  .facts div:nth-child(5) {{ grid-column: 1 / -1; }}
  dl.keys div {{ grid-template-columns: minmax(0, 1fr); gap: 2px; }}
  th, td {{ padding: 8px 6px; }}
  table.wide td:nth-child(2) {{ display: none; }}
}}
@media (prefers-reduced-motion: reduce) {{ html {{ scroll-behavior: auto; }} .mosaic img {{ transition: none; }} }}
</style>
</head>
<body>
<header class="top">
  <div class="wrap">
    <a class="logo" href="#">A22<span>.</span></a>
    <nav aria-label="Sections">
      <a href="#engines">Engines</a><a href="#controls">Controls</a><a href="#tape">Tape</a>
      <a href="#effects">Effects</a><a href="#usb">USB</a><a href="#install">Install</a><a href="#faq">FAQ</a>
      <a href="sim/">Simulator</a>
    </nav>
  </div>
</header>

<main>
<div class="wrap">
  <div class="hero">
    <div>
      <h1>A22<span class="dot">.</span></h1>
      <p class="lede">An FM workstation for the M-VAVE FM-1.</p>
      <p class="sub">Free, open-source firmware: twelve synth and drum engines drawn in colour, eight effects,
        a four-track tape with knob automation, a mixer, and USB audio and MIDI over one cable.</p>
      <div class="ctas">
        <a class="btn primary" href="{RELEASE}">Download the latest release</a>
        <a class="btn" href="sim/">Play it in your browser</a>
        <a class="btn" href="{REPO}">Source on GitHub</a>
        <a class="btn coffee" href="{COFFEE}">☕ Buy me a coffee</a>
      </div>
    </div>
    <div class="mosaic" aria-hidden="true">{mosaic()}</div>
  </div>

  <div class="facts">
    <div><b>12</b><span>engines</span></div>
    <div><b>96</b><span>algorithms</span></div>
    <div><b>8</b><span>effects</span></div>
    <div><b>4</b><span>tape tracks</span></div>
    <div><b>1</b><span>USB cable: audio + MIDI</span></div>
  </div>
  <nav class="chips" aria-label="Engines">{chips()}</nav>
</div>

<section id="intro">
  <div class="wrap prose">
    <div class="label"><span class="n">01</span><h2>How it plays</h2></div>
    <p><b>PRESETS</b> picks the engine, <b>ALGORITHM</b> one of its eight algorithms, and the four knobs shape the
      sound and the picture together. Each algorithm keeps its own knob settings, so they stand where you left them.</p>
    <p>Deeper settings live on small pages that all work the same way: four columns, one for each knob, and SELECT
      for the next subpage. Learn one page and you know them all. Everything saves by itself.</p>
  </div>
</section>

<section id="engines">
  <div class="wrap">
    <div class="label"><span class="n">02</span><h2>The engines</h2></div>
    <p>Almost everything is synthesised. KIT's 909, CRISP, LOFI and WORLD kits layer in short CC0 recordings of real drums.
      The four knobs are coloured as on the FM-1: <b style="color:var(--k1)">blue</b>, <b style="color:var(--k2)">green</b>,
      <b style="color:var(--k3)">white</b>, <b style="color:var(--k4)">orange</b>.</p>
    <div class="engines">{engine_cards()}
    </div>
  </div>
</section>

<section id="controls">
  <div class="wrap">
    <div class="label"><span class="n">03</span><h2>Controls</h2></div>
    <p>A button with a hold does its tap when you let go, so holding one only does its hold.</p>
    <dl class="keys">{hot}</dl>
  </div>
</section>

<section id="tape">
  <div class="wrap">
    <div class="label"><span class="n">04</span><h2>The tape, steps, riffs and chords</h2></div>
    <div class="cols">
      <div class="card">
        <h3>Four-track tape</h3>
        <ul>
          <li>T1 to T4, each with its own engine, algorithm, knobs, volume, pan, send and mute.</li>
          <li>1 to 32 bars, 2,048 events (1,024 notes) across all four tracks, knob moves included.</li>
          <li><b>REC</b> arms the track in hand and the first key starts the take; REC again closes it and the tape plays on. REC while it plays overdubs. <b>PLAY</b> starts and stops it.</li>
          <li>Knobs turned while recording play back as automation.</li>
          <li>SPEED plays it at half or double speed or backwards; TRICK adds a tape brake or stutters.</li>
          <li><b>Hold REC</b> clears the track in hand; keep holding and it asks <b>CLEAR ALL?</b> before it clears the rest.</li>
        </ul>
      </div>
      <div class="card">
        <h3>Step, Riff and Shake</h3>
        <ul>
          <li><b>STEP:</b> hold SEQ to swap the tape for the step sequencer. REC writes, PLAY runs, SELECT moves the cursor, a tap of EDIT is a rest. Up to eight notes a step.</li>
          <li><b>RIFF:</b> record a sequence a note at a time on the SEQ page, then hold any key and it plays from there, transposed.</li>
          <li><b>SHAKE:</b> every key drops a ball into a spinning cage, and it plays its note each time it bounces. GRAV, SPIN, BOUNCE and CAGE shape the cage.</li>
          <li><b>Chord memory:</b> hold a chord and press EDIT + GLO, and each key then plays that shape from itself. Or let A22 build chords from a scale.</li>
        </ul>
      </div>
    </div>
  </div>
</section>

<section id="effects">
  <div class="wrap">
    <div class="label"><span class="n">05</span><h2>Effects and pages</h2></div>
    <p>One effect at a time, chosen on the FX page with ALGORITHM; each track sets how much it sends. The master bus has its own three-band EQ and drive.</p>
    <table class="wide"><thead><tr><th>Effect</th><th>What it is</th><th>Knobs</th></tr></thead><tbody>{effects}</tbody></table>
    <p style="margin-top:28px">Each page's four columns are the four knobs, and SELECT steps through its subpages.</p>
    <table><thead><tr><th>Page</th><th>Button</th><th>Columns</th></tr></thead><tbody>{pages}</tbody></table>
  </div>
</section>

<section id="usb">
  <div class="wrap">
    <div class="label"><span class="n">06</span><h2>USB audio and MIDI</h2></div>
    <div class="cols">
      <div class="card"><h3>Audio</h3><p>Plug it into a computer or phone with a USB cable that carries data and pick the FM-1 as an input: you record the whole mix at the MASTER volume. It only goes one way.</p></div>
      <div class="card"><h3>MIDI in</h3><p>Over USB or the TRS jack, on any channel. Notes play the engine in hand at their velocity, CC 20 to 23 turn the four knobs, CC 123 stops every note.</p></div>
      <div class="card"><h3>MIDI out</h3><p>Over USB on channel 1: the notes of the keys, the arpeggiator and the tape, and CC 20 to 23 as the knobs turn. Notes that came in over MIDI aren't sent back out.</p></div>
      <div class="card"><h3>USB host</h3><p>No computer? Hold OCT − and OCT + for a second and the port turns host for a USB-MIDI keyboard on its own power or a powered hub. Hold both again to go back.</p></div>
    </div>
  </div>
</section>

<section id="install">
  <div class="wrap">
    <div class="label"><span class="n">07</span><h2>Install</h2></div>
    <div class="cols">
      <div class="card">
        <h3>Before you start</h3>
        <ul>
          <li>Back up what the firmware you are leaving has made, with its own backup.</li>
          <li>Charge the FM-1 and use a USB cable that carries data.</li>
          <li>A22 is community firmware with no warranty, and nothing to do with M-VAVE.</li>
        </ul>
      </div>
      <div class="card">
        <h3>From a terminal</h3>
        <p>Download <code>a22-X.Y.Z.fwsc</code> from the <a href="{RELEASE}">latest release</a>, then from a clone of the repository:</p>
        <pre>pip3 install mido python-rtmidi
python3 base/tools/fm1_install.py a22-X.Y.Z.fwsc</pre>
        <p>It works from the stock firmware (V15) and other custom firmwares, asks before it writes, and checks the FM-1 afterwards.</p>
      </div>
    </div>
  </div>
</section>

<section id="faq">
  <div class="wrap">
    <div class="label"><span class="n">08</span><h2>Questions</h2></div>
    {faq}
  </div>
</section>

<section id="support">
  <div class="wrap">
    <div class="support">
      <div>
        <h3>Enjoying A22?</h3>
        <p>It's free and stays free. If it gave you a good evening, a coffee keeps it going.</p>
      </div>
      <a class="btn coffee" href="{COFFEE}">☕ Buy me a coffee</a>
    </div>
  </div>
</section>
</main>

<footer>
  <div class="wrap">
    <p>A22 is <a href="{REPO}/blob/main/LICENSE">MIT</a>, derived from <a href="https://github.com/jakehandy/h1">h1</a> by Jake Handy.
      Its package contains <a href="https://github.com/charlesvestal/fm1-base">fm1-base</a> by Charles Vestal and
      <a href="https://github.com/hugelton/felucca-bsp">felucca-bsp</a> from Felucca by Leo Kuroshita, Hügelton Instruments (MPL-2.0).</p>
    <p>M-VAVE and FM-1 are trademarks of their owners. A22 is not affiliated with M-VAVE.
      <a href="{REPO}">GitHub</a> · <a href="{RELEASE}">Releases</a> · <a href="sim/">Simulator</a> · <a href="{COFFEE}">Buy me a coffee</a></p>
  </div>
</footer>
</body>
</html>
"""


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = sys.argv[1]
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "img"))
    with open(os.path.join(out, "index.html"), "w") as f:
        f.write(page())
    for _, _, pic, *_ in ENGINES:
        shutil.copy(os.path.join(ROOT, "docs", "img", "a22", pic + ".png"), os.path.join(out, "img"))
    shutil.copytree(os.path.join(ROOT, "web"), os.path.join(out, "sim"))
    os.makedirs(os.path.join(out, "dist"))
    shutil.copy(os.path.join(ROOT, "dist", "a22.rfm1"), os.path.join(out, "dist"))
    open(os.path.join(out, ".nojekyll"), "w").close()
    print(f"wrote {out}: index.html, img/ ({len(ENGINES)} pictures), sim/, dist/a22.rfm1")


if __name__ == "__main__":
    main()
