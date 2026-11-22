# CYCLE's wavetables

CYCLE's four banks are made from single-cycle waves of [Adventure Kid Waveforms (AKWF)](https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE) by Kristoffer Ekstrand, CC0 1.0 (public domain dedication: free to use in anything, with no
credit owed; credited here all the same). They are read at commit `8de90bf`.

This file and `tools/akwf_banks.json` are written by `tools/akwf.py`; `tools/gen_wavetables.py` makes
`core/wave_akwf.c` from the JSON. Each wave keeps its first 48 harmonics and takes its bank's shared
phases (see `tools/akwf.py` for why).

## STRING: cello, violin and the string box, dark to bright

| Place | AKWF file |
|---|---|
| 0 | `AKWF_stringbox/AKWF_cheeze_0001.wav` |
| 1 | `AKWF_stringbox/AKWF_cheeze_0002.wav` |
| 2 | `AKWF_stringbox/AKWF_cheeze_0004.wav` |
| 3 | `AKWF_stringbox/AKWF_cheeze_0005.wav` |
| 4 | `AKWF_violin/AKWF_violin_0009.wav` |
| 5 | `AKWF_cello/AKWF_cello_0006.wav` |
| 6 | `AKWF_cello/AKWF_cello_0019.wav` |
| 7 | `AKWF_cello/AKWF_cello_0005.wav` |
| 8 | `AKWF_cello/AKWF_cello_0017.wav` |
| 9 | `AKWF_violin/AKWF_violin_0008.wav` |
| 10 | `AKWF_violin/AKWF_violin_0013.wav` |
| 11 | `AKWF_cello/AKWF_cello_0012.wav` |
| 12 | `AKWF_cello/AKWF_cello_0018.wav` |
| 13 | `AKWF_cello/AKWF_cello_0015.wav` |
| 14 | `AKWF_cello/AKWF_cello_0007.wav` |

## REED: clarinet, flute, alto sax and oboe, dark to bright

| Place | AKWF file |
|---|---|
| 0 | `AKWF_clarinett/AKWF_clarinett_0019.wav` |
| 1 | `AKWF_flute/AKWF_flute_0015.wav` |
| 2 | `AKWF_clarinett/AKWF_clarinett_0007.wav` |
| 3 | `AKWF_altosax/AKWF_altosax_0022.wav` |
| 4 | `AKWF_altosax/AKWF_altosax_0021.wav` |
| 5 | `AKWF_altosax/AKWF_altosax_0014.wav` |
| 6 | `AKWF_altosax/AKWF_altosax_0020.wav` |
| 7 | `AKWF_altosax/AKWF_altosax_0013.wav` |
| 8 | `AKWF_oboe/AKWF_oboe_0005.wav` |
| 9 | `AKWF_clarinett/AKWF_clarinett_0023.wav` |
| 10 | `AKWF_altosax/AKWF_altosax_0011.wav` |
| 11 | `AKWF_altosax/AKWF_altosax_0025.wav` |
| 12 | `AKWF_altosax/AKWF_altosax_0010.wav` |
| 13 | `AKWF_clarinett/AKWF_clarinett_0020.wav` |
| 14 | `AKWF_altosax/AKWF_altosax_0005.wav` |

## KEYS: piano and electric piano, dark to bright

| Place | AKWF file |
|---|---|
| 0 | `AKWF_piano/AKWF_piano_0010.wav` |
| 1 | `AKWF_piano/AKWF_piano_0015.wav` |
| 2 | `AKWF_epiano/AKWF_epiano_0012.wav` |
| 3 | `AKWF_epiano/AKWF_epiano_0010.wav` |
| 4 | `AKWF_epiano/AKWF_epiano_0040.wav` |
| 5 | `AKWF_epiano/AKWF_epiano_0003.wav` |
| 6 | `AKWF_epiano/AKWF_epiano_0052.wav` |
| 7 | `AKWF_piano/AKWF_piano_0028.wav` |
| 8 | `AKWF_epiano/AKWF_epiano_0021.wav` |
| 9 | `AKWF_epiano/AKWF_epiano_0049.wav` |
| 10 | `AKWF_epiano/AKWF_epiano_0032.wav` |
| 11 | `AKWF_piano/AKWF_piano_0018.wav` |
| 12 | `AKWF_epiano/AKWF_epiano_0016.wav` |
| 13 | `AKWF_piano/AKWF_piano_0002.wav` |
| 14 | `AKWF_epiano/AKWF_epiano_0008.wav` |

## VOICE: human voice, dark to bright

| Place | AKWF file |
|---|---|
| 0 | `AKWF_hvoice/AKWF_hvoice_0007.wav` |
| 1 | `AKWF_hvoice/AKWF_hvoice_0024.wav` |
| 2 | `AKWF_hvoice/AKWF_hvoice_0087.wav` |
| 3 | `AKWF_hvoice/AKWF_hvoice_0053.wav` |
| 4 | `AKWF_hvoice/AKWF_hvoice_0004.wav` |
| 5 | `AKWF_hvoice/AKWF_hvoice_0026.wav` |
| 6 | `AKWF_hvoice/AKWF_hvoice_0080.wav` |
| 7 | `AKWF_hvoice/AKWF_hvoice_0039.wav` |
| 8 | `AKWF_hvoice/AKWF_hvoice_0006.wav` |
| 9 | `AKWF_hvoice/AKWF_hvoice_0020.wav` |
| 10 | `AKWF_hvoice/AKWF_hvoice_0078.wav` |
| 11 | `AKWF_hvoice/AKWF_hvoice_0092.wav` |
| 12 | `AKWF_hvoice/AKWF_hvoice_0102.wav` |
| 13 | `AKWF_hvoice/AKWF_hvoice_0032.wav` |
| 14 | `AKWF_hvoice/AKWF_hvoice_0050.wav` |
