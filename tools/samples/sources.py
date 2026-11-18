"""Where h1's recordings come from, and what each bank is cut from.

Every source is CC0 (public domain dedication): free to use in anything, commercial or not, with no
credit owed. They are credited anyway, in docs/SAMPLES.md, which tools/samples/build.py writes from this
file.
"""

VCSL = "https://raw.githubusercontent.com/sgossner/VCSL/master/"
SFZ = "https://raw.githubusercontent.com/sfzinstruments/"

LIBRARIES = {
    "vcsl": dict(
        name="Versilian Community Sample Library (VCSL)",
        by="Versilian Studios LLC (Sam Gossner and contributors)",
        licence="CC0 1.0",
        url="https://github.com/sgossner/VCSL"),
    "cithara": dict(
        name="Cithara Barbarica",
        by="Karoryfer Samples / sfzinstruments",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/cithara-barbarica"),
    "erhu": dict(
        name="AliExpress Erhu",
        by="Karoryfer Samples / sfzinstruments",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/aliexpress-erhu"),
    "squid": dict(
        name="Squidpipes",
        by="Karoryfer Samples (Karoryfer Lecolds)",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/karoryfer.squidpipes"),
    "cello": dict(
        name="Karoryfer x bigcat Cello",
        by="Karoryfer Samples and bigcat Instruments",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/karoryfer-bigcat.cello"),
    "meatbass": dict(
        name="Meatbass",
        by="Karoryfer Samples",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/karoryfer.meatbass"),
    "shiny": dict(
        name="Shinyguitar",
        by="Karoryfer Samples",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/karoryfer.shinyguitar"),
    "ganjo": dict(
        name="Ganjo (SX six-string banjo)",
        by="itsclipping / sfzinstruments",
        licence="CC0 1.0",
        url="https://github.com/sfzinstruments/ganjo"),
    "gm": dict(
        name="Discord SFZ GM Bank: 105 Sitar and 112 Shanai",
        by="Dr. Narayan Bhagawan Raikar (the recordings), the Discord SFZ GM Bank contributors",
        licence="CC0 (stated in each instrument's .sfz)",
        url="https://github.com/sfzinstruments/Discord-SFZ-GM-Bank"),
}


def vcsl(path):
    return ("vcsl", VCSL + path)


def sfz(lib, repo, path):
    return (lib, SFZ + repo + "/" + path)


def folk(n):
    return vcsl(f"Chordophones/Composite Chordophones/Folk Harp/EWHarp_Normal_{n}_v3_RR1.wav")


def concert(n):
    return vcsl(f"Chordophones/Composite Chordophones/Concert Harp/KSHarp_{n}.wav")


def psal(n, rr):
    return vcsl(f"Chordophones/Zithers/Psaltery, Bowed and Plucked/Pluck/BowedPsaltery_{n}_Main_Pluck_rr{rr}.wav")


def organ(n, near):
    return vcsl(f"Aerophones/Edge-blown Aerophones/Renaissance Organ/8'/RenOrgan_8foot_Room_{n}_rr1.wav") + (near,)


def tranh(n):
    return vcsl(f"Chordophones/Zithers/Dan Tranh/Normal/{n}_ff_1.wav")


def cith(n):
    return sfz("cithara", "cithara-barbarica/main", f"Samples/finger/cithara_{n}_finger_rr1.wav")


def erhu(n, near):
    return sfz("erhu", "aliexpress-erhu/main", f"Samples/sus/erhu_{n}_sus_rr1.wav") + (near,)


def squid(n, near):
    return sfz("squid", "karoryfer.squidpipes/master", f"Samples/long_clean/{n}_long_clean.wav") + (near,)


def cello(n, near):
    return sfz("cello", "karoryfer-bigcat.cello/master", f"Samples/sus/{n}_mf_d.wav") + (near,)


def arco(n, near):
    return sfz("meatbass", "karoryfer.meatbass/master", f"Samples/arco_looped/{n}_vl2_down.wav") + (near,)


def pizz(n):
    return sfz("meatbass", "karoryfer.meatbass/master", f"Samples/pizz/{n}_vl3_rr1.wav")


def shiny(n, vl):
    return sfz("shiny", "karoryfer.shinyguitar/master", f"Samples/acoustic/{n}_vl{vl}_rr1_1.wav")


def ganjo(n):
    return sfz("ganjo", "ganjo/main", f"Common/Banjo_Common - {n}.wav")


def sitar(n):
    return sfz("gm", "Discord-SFZ-GM-Bank/master", f"Discord GM/Melodic/105-Sitar/Str{n}.flac")


def shanai(n, near):
    return sfz("gm", "Discord-SFZ-GM-Bank/master", f"Discord GM/Melodic/112-Shanai/Shn{n}.flac") + (near,)


# A bank: (C name, kind, what it is in h1, options, [sources]).
#   kind "pluck": the first moments of a plucked note, with the string taken back out of them, to be fed
#                 into the engine's own string. Options: ms (how much is kept).
#   kind "loop":  the start of a held note and a stretch of it that repeats. Options: attack_ms, loop_ms,
#                 rate (what it is stored at; 22050 unless the recording is dull enough for less). Each
#                 source names the MIDI note the recording is near, which settles its octave.
BANKS = [
    ("LUTE", "pluck", "BARD LUTE: a gut-strung lyre, strung here in pairs",
     dict(ms=100), [cith("e4"), cith("b4"), cith("ab5")]),
    ("FOLKHARP", "pluck", "BARD HARP: a lever harp",
     dict(ms=100), [folk("C2"), folk("C3"), folk("C4"), folk("G#4")]),
    ("PSALTERY", "pluck", "BARD PSALTERY: a psaltery, plucked",
     dict(ms=100), [psal("A#3", 1), psal("E4", 2), psal("C5", 1)]),
    ("ORGAN", "loop", "BARD ORGAN: a Renaissance organ's 8-foot pipes",
     dict(attack_ms=70, loop_ms=110), [organ("C2", 48), organ("C3", 60), organ("C4", 72)]),
    ("PIPES", "loop", "BARD BAGPIPE, and its drones",
     dict(attack_ms=35, loop_ms=110), [squid("g4", 55), squid("g5", 67), squid("g6", 79)]),
    ("SHAWM", "loop", "BARD SHAWM: a shehnai, the shawm's Indian sister",
     dict(attack_ms=50, loop_ms=130), [shanai("01", 48), shanai("08", 60), shanai("12", 67), shanai("15", 72)]),
    ("FIDDLE", "loop", "BARD REBEC: an erhu, a two-string spike fiddle",
     dict(attack_ms=60, loop_ms=150), [erhu("d5", 62), erhu("a5", 69), erhu("d6", 74), erhu("a6", 81)]),
    ("CELLO", "loop", "BARD GURDY, TWANG BOW and BARD's drone under the strings: a cello",
     dict(attack_ms=60, loop_ms=150), [cello("C2", 48), cello("A2", 57), cello("C3", 60), cello("A3", 69), cello("C4", 72)]),
    ("BASSBOW", "loop", "BARD's low drone: a double bass, bowed",
     dict(attack_ms=40, loop_ms=180, rate=11025), [arco("c2", 36)]),
    ("CONCERTHARP", "pluck", "TWANG HARP: a concert harp",
     dict(ms=100), [concert("F2_f1"), concert("G3_f1"), concert("D4_f1"), concert("C5_f1"), concert("D6_f1")]),
    ("GUITAR_SOFT", "pluck", "TWANG NYLON: an acoustic guitar played softly",
     dict(ms=100), [shiny("a2", 1), shiny("a3", 1), shiny("a4", 1), shiny("a5", 1)]),
    ("GUITAR_HARD", "pluck", "TWANG TWELVE: the same guitar played hard, strung here in pairs",
     dict(ms=100), [shiny("a2", 4), shiny("a3", 4), shiny("a4", 4), shiny("a5", 4)]),
    ("SITAR", "pluck", "TWANG SITAR",
     dict(ms=110), [sitar("08"), sitar("15"), sitar("22")]),
    ("BANJO", "pluck", "TWANG BANJO: a six-string banjo",
     dict(ms=90), [ganjo("A3"), ganjo("G4"), ganjo("G5"), ganjo("B5")]),
    ("KOTO", "pluck", "TWANG KOTO: a dan tranh, the koto's Vietnamese sister",
     dict(ms=100), [tranh("B2"), tranh("F#3"), tranh("C#4")]),
    ("BASS", "pluck", "TWANG BASS: a double bass, plucked",
     dict(ms=120), [pizz("a1"), pizz("a2"), pizz("a3")]),
]
