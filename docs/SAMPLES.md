# The recordings in h1

BARD and TWANG are built on recordings of real instruments. Every one of them is CC0: the people
who made them gave them to the public domain, so they can be used in anything, sold or not, with no
credit owed. They are credited here all the same.

This file is written by `tools/samples/build.py` from `tools/samples/sources.py`, with
`core/smp_data.c`. To change a recording, change the sources and run it again.

In the firmware they come to 162,048 bytes: 56 cuts, 8-bit mu-law at 22.05 kHz.

## Libraries

| Library | By | Licence |
|---|---|---|
| [Cithara Barbarica](https://github.com/sfzinstruments/cithara-barbarica) | Karoryfer Samples / sfzinstruments | CC0 1.0 |
| [Versilian Community Sample Library (VCSL)](https://github.com/sgossner/VCSL) | Versilian Studios LLC (Sam Gossner and contributors) | CC0 1.0 |
| [Squidpipes](https://github.com/sfzinstruments/karoryfer.squidpipes) | Karoryfer Samples (Karoryfer Lecolds) | CC0 1.0 |
| [Discord SFZ GM Bank: 105 Sitar and 112 Shanai](https://github.com/sfzinstruments/Discord-SFZ-GM-Bank) | Dr. Narayan Bhagawan Raikar (the recordings), the Discord SFZ GM Bank contributors | CC0 (stated in each instrument's .sfz) |
| [AliExpress Erhu](https://github.com/sfzinstruments/aliexpress-erhu) | Karoryfer Samples / sfzinstruments | CC0 1.0 |
| [Karoryfer x bigcat Cello](https://github.com/sfzinstruments/karoryfer-bigcat.cello) | Karoryfer Samples and bigcat Instruments | CC0 1.0 |
| [Meatbass](https://github.com/sfzinstruments/karoryfer.meatbass) | Karoryfer Samples | CC0 1.0 |
| [Shinyguitar](https://github.com/sfzinstruments/karoryfer.shinyguitar) | Karoryfer Samples | CC0 1.0 |
| [Ganjo (SX six-string banjo)](https://github.com/sfzinstruments/ganjo) | itsclipping / sfzinstruments | CC0 1.0 |

## What is cut from what

A plucked bank keeps only the first tenth of a second of each note, with the string taken back out
of it: the pluck and the instrument's body. The firmware's own string model plays it back, tuned
to the recording's decay. A held bank keeps a note's start and a short stretch that repeats.

### BARD LUTE: a gut-strung lyre, strung here in pairs

`SMP_LUTE`, plucked, 6,615 bytes.

| Note | Recording | Library |
|---|---|---|
| 63.9 | [cithara_e4_finger_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/cithara-barbarica/main/Samples/finger/cithara_e4_finger_rr1.wav) | Cithara Barbarica |
| 71.0 | [cithara_b4_finger_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/cithara-barbarica/main/Samples/finger/cithara_b4_finger_rr1.wav) | Cithara Barbarica |
| 79.8 | [cithara_ab5_finger_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/cithara-barbarica/main/Samples/finger/cithara_ab5_finger_rr1.wav) | Cithara Barbarica |

### BARD HARP: a lever harp

`SMP_FOLKHARP`, plucked, 8,820 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.0 | [EWHarp_Normal_C2_v3_RR1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Folk%20Harp/EWHarp_Normal_C2_v3_RR1.wav) | Versilian Community Sample Library (VCSL) |
| 60.0 | [EWHarp_Normal_C3_v3_RR1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Folk%20Harp/EWHarp_Normal_C3_v3_RR1.wav) | Versilian Community Sample Library (VCSL) |
| 72.0 | [EWHarp_Normal_C4_v3_RR1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Folk%20Harp/EWHarp_Normal_C4_v3_RR1.wav) | Versilian Community Sample Library (VCSL) |
| 80.0 | [EWHarp_Normal_G#4_v3_RR1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Folk%20Harp/EWHarp_Normal_G%234_v3_RR1.wav) | Versilian Community Sample Library (VCSL) |

### BARD PSALTERY: a psaltery, plucked

`SMP_PSALTERY`, plucked, 6,615 bytes.

| Note | Recording | Library |
|---|---|---|
| 70.1 | [BowedPsaltery_A#3_Main_Pluck_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Psaltery%2C%20Bowed%20and%20Plucked/Pluck/BowedPsaltery_A%233_Main_Pluck_rr1.wav) | Versilian Community Sample Library (VCSL) |
| 76.0 | [BowedPsaltery_E4_Main_Pluck_rr2.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Psaltery%2C%20Bowed%20and%20Plucked/Pluck/BowedPsaltery_E4_Main_Pluck_rr2.wav) | Versilian Community Sample Library (VCSL) |
| 84.0 | [BowedPsaltery_C5_Main_Pluck_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Psaltery%2C%20Bowed%20and%20Plucked/Pluck/BowedPsaltery_C5_Main_Pluck_rr1.wav) | Versilian Community Sample Library (VCSL) |

### BARD ORGAN: a Renaissance organ's 8-foot pipes

`SMP_ORGAN`, held, 12,045 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.0 | [RenOrgan_8foot_Room_C2_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Aerophones/Edge-blown%20Aerophones/Renaissance%20Organ/8%27/RenOrgan_8foot_Room_C2_rr1.wav) | Versilian Community Sample Library (VCSL) |
| 60.0 | [RenOrgan_8foot_Room_C3_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Aerophones/Edge-blown%20Aerophones/Renaissance%20Organ/8%27/RenOrgan_8foot_Room_C3_rr1.wav) | Versilian Community Sample Library (VCSL) |
| 72.0 | [RenOrgan_8foot_Room_C4_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Aerophones/Edge-blown%20Aerophones/Renaissance%20Organ/8%27/RenOrgan_8foot_Room_C4_rr1.wav) | Versilian Community Sample Library (VCSL) |

### BARD BAGPIPE, and its drones

`SMP_PIPES`, held, 9,513 bytes.

| Note | Recording | Library |
|---|---|---|
| 54.9 | [g4_long_clean.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.squidpipes/master/Samples/long_clean/g4_long_clean.wav) | Squidpipes |
| 67.1 | [g5_long_clean.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.squidpipes/master/Samples/long_clean/g5_long_clean.wav) | Squidpipes |
| 78.9 | [g6_long_clean.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.squidpipes/master/Samples/long_clean/g6_long_clean.wav) | Squidpipes |

### BARD SHAWM: a shehnai, the shawm's Indian sister

`SMP_SHAWM`, held, 15,816 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.2 | [Shn01.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/112-Shanai/Shn01.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |
| 60.1 | [Shn08.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/112-Shanai/Shn08.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |
| 67.1 | [Shn12.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/112-Shanai/Shn12.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |
| 72.2 | [Shn15.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/112-Shanai/Shn15.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |

### BARD REBEC: an erhu, a two-string spike fiddle

`SMP_FIDDLE`, held, 18,428 bytes.

| Note | Recording | Library |
|---|---|---|
| 62.0 | [erhu_d5_sus_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/aliexpress-erhu/main/Samples/sus/erhu_d5_sus_rr1.wav) | AliExpress Erhu |
| 69.1 | [erhu_a5_sus_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/aliexpress-erhu/main/Samples/sus/erhu_a5_sus_rr1.wav) | AliExpress Erhu |
| 74.4 | [erhu_d6_sus_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/aliexpress-erhu/main/Samples/sus/erhu_d6_sus_rr1.wav) | AliExpress Erhu |
| 81.4 | [erhu_a6_sus_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/aliexpress-erhu/main/Samples/sus/erhu_a6_sus_rr1.wav) | AliExpress Erhu |

### BARD GURDY, TWANG BOW and BARD's drone under the strings: a cello

`SMP_CELLO`, held, 23,177 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.0 | [C2_mf_d.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer-bigcat.cello/master/Samples/sus/C2_mf_d.wav) | Karoryfer x bigcat Cello |
| 57.0 | [A2_mf_d.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer-bigcat.cello/master/Samples/sus/A2_mf_d.wav) | Karoryfer x bigcat Cello |
| 60.0 | [C3_mf_d.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer-bigcat.cello/master/Samples/sus/C3_mf_d.wav) | Karoryfer x bigcat Cello |
| 69.0 | [A3_mf_d.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer-bigcat.cello/master/Samples/sus/A3_mf_d.wav) | Karoryfer x bigcat Cello |
| 72.0 | [C4_mf_d.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer-bigcat.cello/master/Samples/sus/C4_mf_d.wav) | Karoryfer x bigcat Cello |

### BARD's low drone: a double bass, bowed

`SMP_BASSBOW`, held, 2,461 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.0 | [c2_vl2_down.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/master/Samples/arco_looped/c2_vl2_down.wav) | Meatbass |

### TWANG HARP: a concert harp

`SMP_CONCERTHARP`, plucked, 11,025 bytes.

| Note | Recording | Library |
|---|---|---|
| 41.0 | [KSHarp_F2_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Concert%20Harp/KSHarp_F2_f1.wav) | Versilian Community Sample Library (VCSL) |
| 55.0 | [KSHarp_G3_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Concert%20Harp/KSHarp_G3_f1.wav) | Versilian Community Sample Library (VCSL) |
| 61.9 | [KSHarp_D4_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Concert%20Harp/KSHarp_D4_f1.wav) | Versilian Community Sample Library (VCSL) |
| 71.9 | [KSHarp_C5_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Concert%20Harp/KSHarp_C5_f1.wav) | Versilian Community Sample Library (VCSL) |
| 85.9 | [KSHarp_D6_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Composite%20Chordophones/Concert%20Harp/KSHarp_D6_f1.wav) | Versilian Community Sample Library (VCSL) |

### TWANG NYLON: an acoustic guitar played softly

`SMP_GUITAR_SOFT`, plucked, 8,820 bytes.

| Note | Recording | Library |
|---|---|---|
| 45.0 | [a2_vl1_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a2_vl1_rr1_1.wav) | Shinyguitar |
| 57.0 | [a3_vl1_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a3_vl1_rr1_1.wav) | Shinyguitar |
| 69.0 | [a4_vl1_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a4_vl1_rr1_1.wav) | Shinyguitar |
| 81.0 | [a5_vl1_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a5_vl1_rr1_1.wav) | Shinyguitar |

### TWANG TWELVE: the same guitar played hard, strung here in pairs

`SMP_GUITAR_HARD`, plucked, 8,820 bytes.

| Note | Recording | Library |
|---|---|---|
| 45.1 | [a2_vl4_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a2_vl4_rr1_1.wav) | Shinyguitar |
| 57.0 | [a3_vl4_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a3_vl4_rr1_1.wav) | Shinyguitar |
| 69.1 | [a4_vl4_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a4_vl4_rr1_1.wav) | Shinyguitar |
| 81.0 | [a5_vl4_rr1_1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.shinyguitar/master/Samples/acoustic/a5_vl4_rr1_1.wav) | Shinyguitar |

### TWANG SITAR

`SMP_SITAR`, plucked, 7,275 bytes.

| Note | Recording | Library |
|---|---|---|
| 48.1 | [Str08.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/105-Sitar/Str08.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |
| 60.1 | [Str15.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/105-Sitar/Str15.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |
| 72.1 | [Str22.flac](https://raw.githubusercontent.com/sfzinstruments/Discord-SFZ-GM-Bank/master/Discord%20GM/Melodic/105-Sitar/Str22.flac) | Discord SFZ GM Bank: 105 Sitar and 112 Shanai |

### TWANG BANJO: a six-string banjo

`SMP_BANJO`, plucked, 7,936 bytes.

| Note | Recording | Library |
|---|---|---|
| 45.6 | [Banjo_Common - A3.wav](https://raw.githubusercontent.com/sfzinstruments/ganjo/main/Common/Banjo_Common%20-%20A3.wav) | Ganjo (SX six-string banjo) |
| 55.6 | [Banjo_Common - G4.wav](https://raw.githubusercontent.com/sfzinstruments/ganjo/main/Common/Banjo_Common%20-%20G4.wav) | Ganjo (SX six-string banjo) |
| 67.1 | [Banjo_Common - G5.wav](https://raw.githubusercontent.com/sfzinstruments/ganjo/main/Common/Banjo_Common%20-%20G5.wav) | Ganjo (SX six-string banjo) |
| 71.0 | [Banjo_Common - B5.wav](https://raw.githubusercontent.com/sfzinstruments/ganjo/main/Common/Banjo_Common%20-%20B5.wav) | Ganjo (SX six-string banjo) |

### TWANG KOTO: a dan tranh, the koto's Vietnamese sister

`SMP_KOTO`, plucked, 6,615 bytes.

| Note | Recording | Library |
|---|---|---|
| 59.4 | [B2_ff_1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Dan%20Tranh/Normal/B2_ff_1.wav) | Versilian Community Sample Library (VCSL) |
| 66.0 | [F#3_ff_1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Dan%20Tranh/Normal/F%233_ff_1.wav) | Versilian Community Sample Library (VCSL) |
| 72.9 | [C#4_ff_1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Chordophones/Zithers/Dan%20Tranh/Normal/C%234_ff_1.wav) | Versilian Community Sample Library (VCSL) |

### TWANG BASS: a double bass, plucked

`SMP_BASS`, plucked, 7,938 bytes.

| Note | Recording | Library |
|---|---|---|
| 33.0 | [a1_vl3_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/master/Samples/pizz/a1_vl3_rr1.wav) | Meatbass |
| 44.9 | [a2_vl3_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/master/Samples/pizz/a2_vl3_rr1.wav) | Meatbass |
| 56.9 | [a3_vl3_rr1.wav](https://raw.githubusercontent.com/sfzinstruments/karoryfer.meatbass/master/Samples/pizz/a3_vl3_rr1.wav) | Meatbass |
