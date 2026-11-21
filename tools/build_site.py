#!/usr/bin/env python3
"""Builds A22's website into a folder: the front page (index.html), the manual (manual.html), the engines'
screen captures, the FM-1's own Barlow lettering (site/fonts) and the browser simulator.

    python3 tools/build_site.py OUT

.github/workflows/pages.yml runs it and publishes OUT to GitHub Pages (https://abektes.github.io/a22/). The
simulator is web/ with dist/a22.rfm1 beside it, as the repository lays them out (it loads ../dist/a22.rfm1).
The look is a synth's voice-ROM catalogue: twelve banks (A to L, one an engine) of eight voices.
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
SIM = "sim/"
BANKS = "ABCDEFGHIJKL"


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
     [("AETHER", "an octave modulator over a fifth, airy and open"), ("VOX", "the most vowel: a voice inside the drone"),
      ("SOLAR", "a bright twelfth shimmering over a sub-octave"), ("ICE", "inharmonic, glassy FM with two octaves of shimmer"),
      ("MIST", "the softest: little FM, wide and breathy"), ("ABYSS", "an octave down with a low modulator, dark"),
      ("PRISM", "a third-harmonic modulator, bright and faceted"), ("GHOST", "ratios a hair off true, beating, the widest drift")]),
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
        "Yes. Over one USB cable the FM-1 is an audio input (the whole mix at the MASTER volume) and a MIDI port: notes in at their velocity, CC 20 to 23 for the knobs, and the keys, arpeggiator and tape out on channel 1.")]



def esc(s):
    return html.escape(s, quote=True)


def hexc(c):
    return "#%02x%02x%02x" % c


def bank_id(i):
    return "bank-" + BANKS[i].lower()


CSS = """
@font-face { font-family: "Barlow SC"; src: url("fonts/BarlowSemiCondensed-Medium.ttf") format("truetype");
  font-weight: 500; font-style: normal; font-display: swap; }
@font-face { font-family: "Barlow SC"; src: url("fonts/BarlowSemiCondensed-Bold.ttf") format("truetype");
  font-weight: 700; font-style: normal; font-display: swap; }
:root {
  color-scheme: light;
  --paper: #e2e6eb; --sheet: #f5f7f9; --ink: #14171c; --ink-2: #485059; --rule: #b8bfc8; --rule-soft: #d2d7dd;
  --k1: #3b9eff; --k2: #2cc96b; --k3: #ffffff; --k4: #ff6b1a;
  --face: "Barlow SC", "Barlow Semi Condensed", "Arial Narrow", sans-serif;
  --code: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
  --gutter: clamp(16px, 4vw, 48px);
}
* { box-sizing: border-box; }
html { background: var(--paper); scrollbar-color: var(--rule) var(--paper); }
body { margin: 0; background: var(--paper); color: var(--ink); font: 500 18px/1.55 var(--face);
  -webkit-font-smoothing: antialiased; text-rendering: optimizeLegibility; }
::selection { background: var(--ink); color: var(--sheet); }
a { color: var(--ink); text-decoration-thickness: 1px; text-underline-offset: 3px; }
a:hover { text-decoration-thickness: 2px; }
:focus-visible { outline: 2px solid var(--ink); outline-offset: 3px; border-radius: 2px; }
h1, h2, h3 { font-weight: 700; line-height: 1.05; margin: 0; letter-spacing: -0.01em; text-wrap: balance; }
p { margin: 0; }
table { border-collapse: collapse; width: 100%; font-variant-numeric: tabular-nums; }
code, pre { font-family: var(--code); font-size: 15px; }
img { display: block; image-rendering: pixelated; }
.skip { position: absolute; left: -9999px; }
.skip:focus { left: var(--gutter); top: 8px; background: var(--sheet); padding: 8px 12px; z-index: 9; }

/* the masthead: a catalogue's running head */
.masthead { display: flex; align-items: baseline; gap: 28px; padding: 22px var(--gutter);
  border-bottom: 1px solid var(--rule); }
.masthead .mark { font-weight: 700; font-size: 24px; text-decoration: none; letter-spacing: 0; }
.masthead nav { display: flex; gap: 24px; margin-left: auto; font-size: 17px; }
.masthead nav a { color: var(--ink-2); text-decoration: none; }
.masthead nav a:hover, .masthead nav a[aria-current] { color: var(--ink); text-decoration: underline; }

/* the cover: bank tabs down the edge, the title, the open bank */
.cover { display: grid; grid-template-columns: 92px minmax(0, 1fr); min-height: clamp(660px, calc(100svh - 74px), 940px); }
.tabs { display: flex; flex-direction: column; padding: 0; margin: 0; position: sticky; top: 0; align-self: start;
  height: calc(100svh - 74px); min-height: 540px; max-height: 940px; }
.tab { flex: 1 1 0; min-height: 44px; display: flex; flex-direction: column; justify-content: center;
  align-items: flex-start; gap: 0; border: 0; border-bottom: 1px solid rgb(20 23 28 / .18);
  background: var(--ink-c); color: var(--ink); font: inherit; text-align: left; cursor: pointer;
  padding: 4px 0 4px 14px; width: 66px; transition: width .28s cubic-bezier(.16, 1, .3, 1);
  box-shadow: inset -1px 0 0 rgb(20 23 28 / .18); }
.tab b { font-size: 22px; line-height: 1; }
.tab span { font-size: 11px; letter-spacing: .04em; line-height: 1.35; }
.tab:hover { width: 76px; }
.tab[aria-selected="true"] { width: 92px; box-shadow: none; }
.title { padding: clamp(32px, 6vh, 72px) var(--gutter) 0; display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(260px, 380px); align-items: end; gap: 18px 48px; }
.title .side { display: grid; gap: 14px; }
.title h1 { font-size: clamp(64px, 9vw, 96px); line-height: .9; letter-spacing: -0.02em; }
.lede { font-size: clamp(20px, 2.2vw, 26px); line-height: 1.3; max-width: 34ch; margin-top: 14px; }
.actions { display: flex; flex-wrap: wrap; align-items: center; gap: 14px 22px; }
.btn { display: inline-flex; align-items: center; gap: 10px; background: var(--ink); color: var(--sheet);
  font: 700 18px var(--face); padding: 14px 22px; border-radius: 3px; text-decoration: none; }
.btn:hover { background: #2a2f37; color: var(--sheet); }
.more { display: inline-flex; align-items: center; gap: 8px; font-weight: 700; text-decoration: none; }
.more svg, .btn svg { width: 16px; height: 16px; flex: none; }
.more:hover { text-decoration: underline; }
.small { font-size: 14px; color: var(--ink-2); line-height: 1.45; }

/* the sheet's paper runs from the top of the cover beside the rail, so every open tab meets its page */
.cover > .body { background: var(--sheet); margin: 0 var(--gutter) clamp(32px, 6vh, 64px) 0;
  border: 1px solid var(--rule); border-left: 0; border-top: 0; border-radius: 0 0 3px 0; }
.sheet { margin-top: clamp(28px, 5vh, 48px); }
.bank { display: grid; grid-template-columns: auto minmax(0, 1fr); gap: 0 clamp(24px, 3vw, 44px);
  padding: 0 clamp(20px, 3vw, 32px) clamp(20px, 3vw, 32px) var(--gutter); }
.band { grid-column: 1 / -1; display: flex; align-items: baseline; justify-content: space-between; gap: 16px;
  background: var(--ink-c); margin: 0 calc(-1 * clamp(20px, 3vw, 32px)) clamp(20px, 3vw, 28px) calc(-1 * var(--gutter));
  padding: 12px clamp(20px, 3vw, 32px) 12px var(--gutter); border-block: 1px solid rgb(20 23 28 / .25); }
.band h2 { font-size: clamp(28px, 3vw, 38px); display: flex; align-items: baseline; gap: 14px; }
.band .code { font-size: 15px; font-weight: 700; font-variant-numeric: tabular-nums; }
.voices th { text-align: left; font-size: 13px; font-weight: 700; color: var(--ink-2); padding: 0 10px 6px 0; }
.bank[hidden] { display: none; }
.screen { background: #000; padding: 10px; border-radius: 4px; align-self: start; }
.screen img { width: 240px; height: 240px; }
.bank .what { margin: 0 0 18px; max-width: 60ch; color: var(--ink-2); }
.voices td { padding: 7px 10px 7px 0; border-top: 1px solid var(--rule-soft); vertical-align: baseline; }
.voices tr:last-child td { border-bottom: 1px solid var(--rule-soft); }
.voices .n { width: 3.2ch; color: var(--ink-2); }
.voices .v { width: 11ch; font-weight: 700; }
.voices .d { color: var(--ink-2); }
.knobrow { display: flex; flex-wrap: wrap; gap: 6px 22px; margin-top: 16px; font-size: 16px; }
.knob { display: inline-flex; align-items: center; gap: 8px; font-weight: 700; }
.dot { width: 11px; height: 11px; border-radius: 50%; background: var(--c); box-shadow: inset 0 0 0 1px rgb(20 23 28 / .35); }
.bank .open { margin-top: 18px; font-size: 16px; }
@keyframes print { from { opacity: .4; transform: translateY(4px); } }
.printing .voices tr, .printing .screen { animation: print .42s cubic-bezier(.16, 1, .3, 1) both;
  animation-delay: calc(var(--i, 0) * 28ms); }

/* the pages after the cover */
.part { padding: clamp(56px, 9vw, 112px) var(--gutter); border-top: 1px solid var(--rule); }
.part > .inner { display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1.6fr) minmax(180px, .7fr);
  gap: 28px clamp(32px, 5vw, 72px); }
.part .note { font-size: 14px; color: var(--ink-2); line-height: 1.45; border-top: 1px solid var(--rule); padding-top: 14px; align-self: start; }
.part .note p + p { margin-top: 10px; }
.part h2 { font-size: clamp(30px, 3.4vw, 44px); }
.part .aside { margin-top: 14px; max-width: 34ch; color: var(--ink-2); }
.spec th, .spec td { text-align: left; vertical-align: baseline; padding: 14px 0; border-top: 1px solid var(--rule); }
.spec tr:last-child th, .spec tr:last-child td { border-bottom: 1px solid var(--rule); }
.spec th { width: 9.5em; padding-right: 20px; font-weight: 700; }
.steps { list-style: none; margin: 0; padding: 0; counter-reset: step; }
.steps li { counter-increment: step; display: grid; grid-template-columns: 2.2em minmax(0, 1fr); gap: 4px 12px;
  padding: 16px 0; border-top: 1px solid var(--rule); }
.steps li:last-child { border-bottom: 1px solid var(--rule); }
.steps li::before { content: counter(step); font-weight: 700; font-variant-numeric: tabular-nums; }
.steps pre { grid-column: 2; margin: 8px 0 0; padding: 12px 14px; background: var(--sheet); border: 1px solid var(--rule);
  border-radius: 3px; overflow-x: auto; }
.prose p + p { margin-top: 14px; }
.prose p { max-width: 64ch; }

footer { padding: 28px var(--gutter) 44px; border-top: 1px solid var(--rule); display: flex; flex-wrap: wrap;
  gap: 8px 24px; font-size: 14px; color: var(--ink-2); }
footer a { color: var(--ink-2); }
footer .sep { flex: 1 1 auto; }

/* the manual */
.manual { display: grid; grid-template-columns: 250px minmax(0, 1fr); align-items: start; }
.index { position: sticky; top: 0; max-height: 100svh; overflow-y: auto; padding: 28px 20px 28px var(--gutter);
  border-right: 1px solid var(--rule); font-size: 16px; }
.index h2 { font-size: 15px; letter-spacing: .02em; color: var(--ink-2); margin: 18px 0 8px; font-weight: 700; }
.index h2:first-child { margin-top: 0; }
.index ul { list-style: none; margin: 0; padding: 0; }
.index li a { display: flex; align-items: center; gap: 10px; padding: 3px 0; text-decoration: none; color: var(--ink); }
.index li a:hover { text-decoration: underline; }
.swatch { width: 18px; height: 18px; display: inline-grid; place-items: center; font-size: 12px; font-weight: 700;
  background: var(--ink-c); border: 1px solid rgb(20 23 28 / .25); border-radius: 2px; flex: none; }
.pages { padding: 0 var(--gutter) 64px; min-width: 0; }
.chapter { padding: 64px 0 24px; max-width: 980px; scroll-margin-top: 16px; }
.chapter > h2 { font-size: clamp(32px, 3.6vw, 46px); margin-bottom: 14px; }
.chapter > p { max-width: 64ch; color: var(--ink-2); }
.mbank { display: grid; grid-template-columns: 260px minmax(0, 1fr); gap: 0 36px; padding: 40px 0;
  border-top: 1px solid var(--rule); scroll-margin-top: 16px; }
.mbank:first-of-type { border-top: 0; }
.mbank h3 { font-size: 30px; display: flex; align-items: baseline; gap: 12px; }
.mbank h3 .swatch { width: 1.2em; height: 1.2em; font-size: .7em; }
.mbank .what { margin: 8px 0 16px; color: var(--ink-2); max-width: 62ch; }
.mbank h4 { font-size: 15px; font-weight: 700; color: var(--ink-2); margin: 20px 0 4px; letter-spacing: .02em; }
.knobs td { padding: 7px 12px 7px 0; border-top: 1px solid var(--rule-soft); vertical-align: baseline; }
.knobs tr:last-child td { border-bottom: 1px solid var(--rule-soft); }
.knobs .k { width: 10.5ch; font-weight: 700; white-space: nowrap; }
.knobs .k .dot { display: inline-block; margin-right: 8px; vertical-align: -1px; }
.knobs .d { color: var(--ink-2); }
.chart td, .chart th { text-align: left; vertical-align: baseline; padding: 10px 16px 10px 0; border-top: 1px solid var(--rule-soft); }
.chart tr:last-child td, .chart tr:last-child th { border-bottom: 1px solid var(--rule-soft); }
.chart th { font-weight: 700; white-space: nowrap; width: 1%; }
.chart td { color: var(--ink-2); }
.chart thead th { color: var(--ink-2); font-size: 15px; border-top: 0; }
.two { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 8px 48px; }
.two h3 { font-size: 22px; margin: 24px 0 8px; }
.two p { color: var(--ink-2); }
.chapter details { border-top: 1px solid var(--rule-soft); padding: 14px 0; max-width: 72ch; }
.chapter details:last-of-type { border-bottom: 1px solid var(--rule-soft); }
summary { cursor: pointer; font-weight: 700; }
.chapter details p { margin-top: 8px; color: var(--ink-2); }
.toc-toggle { display: none; }

@media (max-width: 1180px) {
  .part > .inner { grid-template-columns: minmax(0, 1fr) minmax(0, 1.6fr); }
  .part .note { grid-column: 2; }
}
@media (max-width: 980px) {
  .manual { grid-template-columns: minmax(0, 1fr); }
  .index { position: static; max-height: none; border-right: 0; border-bottom: 1px solid var(--rule); padding: 0; }
  .index > details { border: 0; padding: 14px var(--gutter); max-width: none; }
  .index > details > div { padding-top: 6px; columns: 2; column-gap: 24px; }
  .mbank { grid-template-columns: minmax(0, 1fr); gap: 16px; }
  .two { grid-template-columns: minmax(0, 1fr); }
}
@media (min-width: 981px) { .index > details > summary { display: none; } }
@media (max-width: 820px) {
  .cover { grid-template-columns: minmax(0, 1fr); min-height: 0; }
  .tabs { flex-direction: row; overflow-x: auto; order: 2; position: static; height: auto; min-height: 0; max-height: none;
    margin: 28px 0 0 var(--gutter); border: 1px solid var(--rule); border-right: 0; border-radius: 3px 0 0 0;
    scroll-snap-type: x mandatory; scroll-padding-left: 0; }
  .tab { width: auto; min-width: 54px; height: 64px; flex: 0 0 auto; border-bottom: 0; border-right: 1px solid rgb(20 23 28 / .18);
    padding: 8px 9px; transition: none; scroll-snap-align: start; box-shadow: none; }
  .tab:hover, .tab[aria-selected="true"] { width: auto; }
  .tab[aria-selected="true"] { box-shadow: inset 0 -4px 0 var(--ink); }
  .cover > .body { display: contents; }
  .title { order: 1; grid-template-columns: minmax(0, 1fr); }
  .sheet { order: 3; margin: 0 var(--gutter) 40px; background: var(--sheet); border: 1px solid var(--rule);
    border-top: 0; border-radius: 0 0 3px 3px; }
  .bank { grid-template-columns: minmax(0, 1fr); gap: 18px; padding: 0 18px 20px; }
  .band { margin: 0 -18px 0; padding: 12px 18px; }
  .title .side { gap: 12px; }
  .part .note { grid-column: 1; }
  .screen { justify-self: start; }
  .part > .inner { grid-template-columns: minmax(0, 1fr); }
}
@media (max-width: 420px) {
  .voices thead { display: none; }
  .voices tr { display: grid; grid-template-columns: 3.4ch minmax(0, 1fr); border-top: 1px solid var(--rule-soft); }
  .voices tr:last-child { border-bottom: 1px solid var(--rule-soft); }
  .voices td, .voices tr:last-child td { border: 0; }
  .voices td.n { grid-row: 1 / span 2; padding-top: 8px; }
  .voices td.v { padding: 8px 0 0; width: auto; }
  .voices td.d { grid-column: 2; padding: 0 0 8px; }
  .spec th { width: auto; display: block; border-top: 1px solid var(--rule); padding-bottom: 0; }
  .spec td { display: block; border-top: 0; padding-top: 4px; }
  .spec tr:last-child th { border-bottom: 0; }
}
@media (prefers-reduced-motion: reduce) {
  .tab { transition: none; }
  .printing .voices tr, .printing .screen { animation: none; }
}
"""

ARROW = ('<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M2 8h11M9 3.5 13.5 8 9 12.5" fill="none" '
         'stroke="currentColor" stroke-width="1.8" stroke-linecap="square"/></svg>')
DOWN = ('<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M8 2v10M3.5 8.5 8 13l4.5-4.5M2.5 15h11" fill="none" '
        'stroke="currentColor" stroke-width="1.8" stroke-linecap="square"/></svg>')

KNOB_VARS = ["var(--k1)", "var(--k2)", "var(--k3)", "var(--k4)"]


def head(title, desc, extra_style=""):
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)}</title>
<meta name="description" content="{esc(desc)}">
<meta property="og:title" content="{esc(title)}">
<meta property="og:description" content="{esc(desc)}">
<meta property="og:image" content="img/fm4.png">
<meta name="theme-color" content="#e2e6eb">
<link rel="icon" href="img/fm4.png">
<link rel="preload" href="fonts/BarlowSemiCondensed-Bold.ttf" as="font" type="font/ttf" crossorigin>
<link rel="preload" href="fonts/BarlowSemiCondensed-Medium.ttf" as="font" type="font/ttf" crossorigin>
<style>{CSS}{extra_style}</style>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>
"""


def masthead(current):
    def link(href, label, key):
        cur = ' aria-current="page"' if key == current else ""
        return f'<a href="{href}"{cur}>{label}</a>'
    return f"""<header class="masthead">
  <a class="mark" href="./">A22</a>
  <nav aria-label="Site">{link("manual.html", "Manual", "manual")}{link(SIM, "Simulator", "sim")}{link(REPO, "GitHub", "gh")}</nav>
</header>
"""


def footer():
    return f"""<footer>
  <span>A22 · free and open source, <a href="{REPO}/blob/main/LICENSE">MIT</a></span>
  <span class="sep"></span>
  <a href="{REPO}">GitHub</a>
  <a href="{COFFEE}">Buy me a coffee</a>
  <span>M-VAVE and FM-1 are trademarks of their owners; A22 is not affiliated with M-VAVE.</span>
</footer>
"""


def front():
    tabs, panels = [], []
    for i, (name, ink, pic, what, knobs, algos) in enumerate(ENGINES):
        L, sel = BANKS[i], i == 0
        tabs.append(f'<button class="tab" role="tab" id="tab-{L.lower()}" aria-controls="panel-{L.lower()}" '
                    f'aria-selected="{"true" if sel else "false"}" tabindex="{0 if sel else -1}" '
                    f'style="--ink-c:{hexc(ink)}"><b>{L}</b><span>{esc(name)}</span></button>')
        rows = "".join(f'<tr style="--i:{j + 1}"><td class="n">{L}{j + 1}</td><td class="v">{esc(a)}</td>'
                       f'<td class="d">{esc(d)}</td></tr>' for j, (a, d) in enumerate(algos))
        kn = "".join(f'<span class="knob"><span class="dot" style="--c:{KNOB_VARS[k]}"></span>{esc(n)}</span>'
                     for k, (n, _) in enumerate(knobs))
        panels.append(f"""
      <div class="bank" role="tabpanel" id="panel-{L.lower()}" aria-labelledby="tab-{L.lower()}"{"" if sel else " hidden"} style="--ink-c:{hexc(ink)}">
        <div class="band"><h2>{L} · {esc(name)}</h2><span class="code">Bank {L} · {L}1–{L}8</span></div>
        <div class="screen"><img src="img/{pic}.png" width="240" height="240" alt="{esc(name)} on the FM-1's screen, playing {esc(algos[0][0])}" loading="{"eager" if sel else "lazy"}"></div>
        <div>
          <p class="what">{esc(what)}</p>
          <table class="voices"><caption class="skip">Bank {L}: {esc(name)}'s eight algorithms</caption><thead><tr><th scope="col">No.</th><th scope="col">Voice</th><th scope="col">Character</th></tr></thead><tbody>{rows}</tbody></table>
          <div class="knobrow" aria-label="Its four knobs">{kn}</div>
          <p class="open"><a class="more" href="manual.html#{bank_id(i)}">{esc(name)} in the manual {ARROW}</a></p>
        </div>
      </div>""")
    spec = "".join(f"<tr><th scope=\"row\">{esc(k)}</th><td>{esc(v)}</td></tr>" for k, v in [
        ("Sound", "Twelve engines, eight algorithms each, four live knobs. Eight effects and a master EQ with drive."),
        ("Making music", "A four-track tape with knob automation, a step sequencer, Riff and Shake, an arpeggiator, "
                         "chord memory and eight snapshots."),
        ("Pictures", "Every engine draws its own animated colour picture on the FM-1's screen, moving with the sound."),
        ("Connect", "USB audio and MIDI over one cable, MIDI in on the TRS jack, USB host for a keyboard."),
        ("Runs on", "The M-VAVE FM-1, with either flash chip. Not the SMK-37.")])
    return head("A22 · firmware for the M-VAVE FM-1",
                "A22 is free, open-source firmware for the M-VAVE FM-1: twelve sound engines, ninety-six algorithms, "
                "a four-track tape, eight effects, USB audio and MIDI.") + masthead("home") + f"""<main id="main">
<section class="cover" aria-label="A22">
  <div class="tabs" role="tablist" aria-label="Banks, one an engine" aria-orientation="vertical">{"".join(tabs)}</div>
  <div class="body">
    <div class="title">
      <div>
        <h1>A22</h1>
        <p class="lede">Firmware for the M-VAVE FM-1. Twelve banks of sound, ninety-six voices.</p>
      </div>
      <div class="side">
        <div class="actions">
          <a class="btn" href="{RELEASE}">{DOWN}Download 0.1.0</a>
          <a class="more" href="{SIM}">Play it in your browser {ARROW}</a>
        </div>
        <p class="small">Free and open source. Installs over USB from the stock firmware or another custom one, and goes back the same way.</p>
      </div>
    </div>
    <div class="sheet">{"".join(panels)}
    </div>
  </div>
</section>

<section class="part" aria-labelledby="inside">
  <div class="inner">
    <div><h2 id="inside">What's inside</h2>
      <p class="aside">A22 turns the FM-1 into an FM workstation. Everything is synthesised, apart from short drum recordings layered into four of KIT's kits.</p></div>
    <table class="spec">{spec}</table>
    <aside class="note"><p>The firmware image is 406 KB of the FM-1's 568 KB slot and uses 440 KB of its 475 KB of memory.</p>
      <p>Package identity FM-1_4122.</p></aside>
  </div>
</section>

<section class="part" aria-labelledby="install">
  <div class="inner">
    <div><h2 id="install">Install</h2>
      <p class="aside">About two minutes, over a USB cable that carries data. Back up anything the firmware you are leaving has made first.</p></div>
    <ol class="steps">
      <li><span>Download <a href="{RELEASE}">a22-0.1.0.fwsc</a> from the latest release, and clone the repository for its installer.</span></li>
      <li><span>Install the MIDI library the installer uses.</span><pre>pip3 install mido python-rtmidi</pre></li>
      <li><span>Plug in the FM-1 and run the installer. It asks before it writes and checks the FM-1 afterwards.</span><pre>python3 base/tools/fm1_install.py a22-0.1.0.fwsc</pre></li>
    </ol>
    <aside class="note"><p>Works from the stock firmware (V15) and from other custom firmwares. On anything older than V15, update with M-VAVE's own updater first.</p></aside>
  </div>
</section>

<section class="part" aria-labelledby="back">
  <div class="inner">
    <div><h2 id="back">Going back</h2></div>
    <div class="prose">
      <p>A22 carries an update loader, so any installer can replace it: press Back to official V15 on <a href="{HUB}">The Firmware Hub</a>, or install another custom firmware.</p>
      <p>If an install stops halfway, plug in and run it again; it picks up where it stopped. If A22 ever fails to start twice, it comes up in safe mode with USB working, and holding REC while you switch on opens the chip's own update mode, which nothing can overwrite.</p>
      <p>The <a href="manual.html">manual</a> has every engine, control and page.</p>
    </div>
    <aside class="note"><p>A22 is early: installed and run on one FM-1 so far.</p>
      <p>Measured there: 41 frames a second on average over all 96 algorithms, and no crash over a 30-minute run.</p></aside>
  </div>
</section>
</main>
""" + footer() + """<script>
(() => {
  const tabs = [...document.querySelectorAll('.tab')];
  const sheet = document.querySelector('.sheet');
  const vertical = () => matchMedia('(min-width: 821px)').matches;
  function open(i, focus) {
    tabs.forEach((t, j) => {
      const on = j === i;
      t.setAttribute('aria-selected', on);
      t.tabIndex = on ? 0 : -1;
      document.getElementById(t.getAttribute('aria-controls')).hidden = !on;
    });
    sheet.classList.remove('printing'); void sheet.offsetWidth; sheet.classList.add('printing');
    if (focus) tabs[i].focus();
    if (!vertical()) tabs[i].scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => open(i, false));
    t.addEventListener('keydown', e => {
      const n = tabs.length;
      const next = { ArrowDown: i + 1, ArrowRight: i + 1, ArrowUp: i - 1, ArrowLeft: i - 1, Home: 0, End: n - 1 }[e.key];
      if (next === undefined) return;
      e.preventDefault();
      open((next + n) % n, true);
    });
  });
  const m = location.hash.match(/^#bank-([a-l])$/);
  if (m) open('abcdefghijkl'.indexOf(m[1]), false);
})();
</script>
</body>
</html>
"""


def manual():
    idx_banks = "".join(f'<li><a href="#{bank_id(i)}"><span class="swatch" style="--ink-c:{hexc(ink)}">{BANKS[i]}</span>{esc(n)}</a></li>'
                        for i, (n, ink, *_) in enumerate(ENGINES))
    chapters = [("controls", "Controls"), ("tape", "Tape and sequencers"), ("effects", "Effects"),
                ("pages", "Pages"), ("usb", "USB audio and MIDI"), ("install", "Install and recovery"),
                ("questions", "Questions")]
    idx_rest = "".join(f'<li><a href="#{k}">{esc(v)}</a></li>' for k, v in chapters)
    banks = []
    for i, (name, ink, pic, what, knobs, algos) in enumerate(ENGINES):
        L = BANKS[i]
        krows = "".join(f'<tr><td class="k"><span class="dot" style="--c:{KNOB_VARS[k]}"></span>K{k + 1} {esc(n)}</td>'
                        f'<td class="d">{esc(d)}</td></tr>' for k, (n, d) in enumerate(knobs))
        vrows = "".join(f'<tr><td class="n">{L}{j + 1}</td><td class="v">{esc(a)}</td><td class="d">{esc(d)}</td></tr>'
                        for j, (a, d) in enumerate(algos))
        banks.append(f"""
    <article class="mbank" id="{bank_id(i)}">
      <div class="screen"><img src="img/{pic}.png" width="200" height="200" alt="{esc(name)} on the FM-1's screen" loading="lazy"></div>
      <div>
        <h3><span class="swatch" style="--ink-c:{hexc(ink)}" aria-hidden="true">{L}</span>{esc(name)}</h3>
        <p class="what">{esc(what)}</p>
        <h4>Knobs</h4>
        <table class="knobs">{krows}</table>
        <h4>Algorithms</h4>
        <table class="voices">{vrows}</table>
      </div>
    </article>""")
    hot = "".join(f'<tr><th scope="row">{esc(k)}</th><td>{esc(v)}</td></tr>' for k, v in HOTKEYS)
    fx = "".join(f'<tr><th scope="row">{esc(n)}</th><td>{esc(d)}</td><td>{esc(k)}</td></tr>' for n, d, k in EFFECTS)
    pages = "".join(f'<tr><th scope="row">{esc(n)}</th><td>{esc(b)}</td><td>{esc(c)}</td></tr>' for n, b, c in PAGES)
    faq = "".join(f"<details><summary>{esc(q)}</summary><p>{esc(a)}</p></details>" for q, a in FAQ)
    return head("A22 manual", "The A22 manual: every engine's knobs and algorithms, the controls, the tape, effects, "
                "pages, USB audio and MIDI, install and recovery.") + masthead("manual") + f"""<main id="main" class="manual">
  <nav class="index" aria-label="Manual contents">
    <details open><summary>Contents</summary><div>
      <h2>Banks</h2><ul>{idx_banks}</ul>
      <h2>Playing</h2><ul>{idx_rest}</ul>
    </div></details>
  </nav>
  <div class="pages">
    <section class="chapter" aria-labelledby="banks">
      <h2 id="banks">Banks</h2>
      <p>PRESETS picks the engine and ALGORITHM one of its eight algorithms. The four knobs shape the sound and the picture
        together, and each algorithm keeps its own knob settings. On the FM-1 the knobs are coloured blue, green, white and orange.</p>
      {"".join(banks)}
    </section>

    <section class="chapter" id="controls" aria-labelledby="h-controls">
      <h2 id="h-controls">Controls</h2>
      <p>A button with a hold does its tap when you let go, so holding one only does its hold.</p>
      <table class="chart">{hot}</table>
    </section>

    <section class="chapter" id="tape" aria-labelledby="h-tape">
      <h2 id="h-tape">Tape and sequencers</h2>
      <div class="two">
        <div><h3>The tape</h3>
          <p>Four tracks, T1 to T4, each with its own engine, algorithm, knobs, volume, pan, send and mute. 1 to 32 bars and
            2,048 events (1,024 notes) across all four tracks, knob moves included; when it is full the screen says TAPE FULL.</p>
          <p style="margin-top:12px">REC arms the track in hand and the first key starts the take; REC again closes it and the tape plays on.
            REC while it plays overdubs, PLAY starts and stops it. Knobs turned while recording play back as automation. SPEED plays
            it at half or double speed or backwards; TRICK adds a tape brake or stutters.</p>
          <p style="margin-top:12px">Hold REC to clear the track in hand. Keep holding and it asks CLEAR ALL?: tap REC within three
            seconds to clear the whole tape. Any other button, or waiting, keeps it.</p></div>
        <div><h3>Step</h3>
          <p>Hold SEQ to swap the tape for the step sequencer. REC turns step writing on and off, PLAY runs the pattern,
            SELECT moves the cursor and a tap of EDIT leaves a rest. Up to eight notes a step.</p>
          <h3>Riff and Shake</h3>
          <p>Modes of the SEQ page (ARP, then ALGORITHM, or hold EDIT and turn ALGORITHM anywhere). RIFF: REC records a sequence
            a note at a time, EDIT leaves a rest, REC finishes; then hold any key and it plays from there, transposed.
            SHAKE: every key drops a ball into a spinning cage that plays its note each time it bounces.</p>
          <h3>Chord memory</h3>
          <p>Hold a chord and press EDIT + GLO: each key then plays that shape from itself. The CHORD page can also build chords
            from the KEYS page's scale, or play a fixed shape.</p></div>
      </div>
    </section>

    <section class="chapter" id="effects" aria-labelledby="h-effects">
      <h2 id="h-effects">Effects</h2>
      <p>One effect at a time, chosen on the FX page with ALGORITHM; each track sets how much it sends. The master bus has its own
        three-band EQ and drive.</p>
      <table class="chart"><thead><tr><th>Effect</th><th>What it is</th><th>Knobs</th></tr></thead><tbody>{fx}</tbody></table>
    </section>

    <section class="chapter" id="pages" aria-labelledby="h-pages">
      <h2 id="h-pages">Pages</h2>
      <p>Each page's four columns are the four knobs, and SELECT steps through its subpages. HOME goes back.</p>
      <table class="chart"><thead><tr><th>Page</th><th>Button</th><th>Columns</th></tr></thead><tbody>{pages}</tbody></table>
    </section>

    <section class="chapter" id="usb" aria-labelledby="h-usb">
      <h2 id="h-usb">USB audio and MIDI</h2>
      <div class="two">
        <div><h3>Audio</h3><p>Pick the FM-1 as an input on a computer or phone and record the whole mix at the MASTER volume.
          It only goes one way: the FM-1 isn't a sound card.</p></div>
        <div><h3>MIDI in</h3><p>Over USB or the TRS jack, on any channel: notes at their velocity, CC 20 to 23 for the four knobs,
          CC 123 to stop every note.</p></div>
        <div><h3>MIDI out</h3><p>Over USB on channel 1: the notes of the keys, the arpeggiator and the tape, and CC 20 to 23 as the
          knobs turn. Notes that came in over MIDI aren't sent back out.</p></div>
        <div><h3>USB host</h3><p>Hold OCT − and OCT + for a second and the port turns host for a USB-MIDI keyboard with its own power
          or a powered hub. Hold both again to go back.</p></div>
      </div>
    </section>

    <section class="chapter" id="install" aria-labelledby="h-install">
      <h2 id="h-install">Install and recovery</h2>
      <div class="two">
        <div><h3>Install</h3><p>Download a22-X.Y.Z.fwsc from the <a href="{RELEASE}">latest release</a>, then from a clone of the
          repository run <code>pip3 install mido python-rtmidi</code> and <code>python3 base/tools/fm1_install.py a22-X.Y.Z.fwsc</code>.
          It works from the stock firmware (V15) and other custom firmwares.</p></div>
        <div><h3>If something goes wrong</h3><p>An install that stopped halfway resumes when you run it again. After two failed
          starts A22 comes up in SAFE MODE with USB working. Holding REC while you switch on enters the chip's own update mode;
          the Hub's <a href="{HUB}/tools/rescue/">rescue page</a> takes it from there.</p></div>
      </div>
    </section>

    <section class="chapter" id="questions" aria-labelledby="h-questions">
      <h2 id="h-questions">Questions</h2>
      {faq}
    </section>
  </div>
</main>
""" + footer() + """</body>
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
        f.write(front())
    with open(os.path.join(out, "manual.html"), "w") as f:
        f.write(manual())
    for _, _, pic, *_ in ENGINES:
        shutil.copy(os.path.join(ROOT, "docs", "img", "a22", pic + ".png"), os.path.join(out, "img"))
    shutil.copytree(os.path.join(ROOT, "site", "fonts"), os.path.join(out, "fonts"))
    shutil.copytree(os.path.join(ROOT, "web"), os.path.join(out, "sim"))
    os.makedirs(os.path.join(out, "dist"))
    shutil.copy(os.path.join(ROOT, "dist", "a22.rfm1"), os.path.join(out, "dist"))
    open(os.path.join(out, ".nojekyll"), "w").close()
    print(f"wrote {out}: index.html, manual.html, img/ ({len(ENGINES)} screens), fonts/, sim/, dist/a22.rfm1")


if __name__ == "__main__":
    main()
