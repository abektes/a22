# The recordings in A22

KIT's 909, CRISP, LOFI and WORLD kits layer short recorded hits under their synthesised drums, and SKINS'
two kits (STUDIO and ORCH, each four ways) are made of nothing else. Every hit is CC0: the people who made
the recordings gave them to the public domain, so they can be used in anything, sold or not, with no
credit owed. They are credited here all the same. (CYCLE's single cycles are in
[WAVETABLES.md](WAVETABLES.md).)

`tools/samples/drums.py` cuts the hits from `sources.DRUMS` in `tools/samples/sources.py` into
`core/plunk_data.c`: 47 hits, about 44,233 bytes, 4-bit ADPCM at the rate each one's top allows. A ringing
hit keeps its start and a short stretch that repeats.

This file is kept by hand. `tools/samples/build.py` is h1's: it would rewrite this file with h1's BARD and
TWANG recordings, which A22 does not carry, and write `core/smp_data.c`, which A22 does not use.

## Libraries

| Library | By | Licence |
|---|---|---|
| [Versilian Community Sample Library (VCSL)](https://github.com/sgossner/VCSL) | Versilian Studios LLC (Sam Gossner and contributors) | CC0 1.0 |
| [Virtuosity Drums](https://github.com/sfzinstruments/virtuosity_drums) | Versilian Studios and Karoryfer Samples: drummer Austin McMahon on the house kit of Virtuosity Musical Instruments, Boston | CC0 1.0 |

## The hits

The hits from `VZ_KICK` on were added for SKINS.

| Hit | What it is | Kept | Recording |
|---|---|---|---|
| `VZ_GONG` | a gong | 11.025 kHz, 300 ms | [gong_f.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Gong%201/gong_f.wav) |
| `VZ_BELL` | a Nepalese hand bell | 22.05 kHz, 69 ms | [HB_2.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Hand%20Bells%2C%20Nepalese/HB_2.wav) |
| `VZ_ZILL` | finger cymbals | 22.05 kHz, 58 ms | [Fing_Cymb.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Finger%20Cymbals/Fing_Cymb.wav) |
| `VZ_DUM` | a darbuka, the low stroke | 8 kHz, 190 ms | [Darbuka_1_hit_vl2_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Darbuka/Darbuka_1_hit_vl2_rr1.wav) |
| `VZ_TEK` | a darbuka, the rim stroke | 16 kHz, 80 ms | [Darbuka_2_hit_vl2_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Darbuka/Darbuka_2_hit_vl2_rr1.wav) |
| `VZ_FRAME` | a frame drum | 8 kHz, 220 ms | [HDrumL_Hit_v3_rr1_Sum.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Frame%20Drum/HDrumL_Hit_v3_rr1_Sum.wav) |
| `VZ_LOG` | a slit drum | 8 kHz, 150 ms | [LogDrumLo_MedM_v3_rr1_Sum.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Slit%20Drum/LogDrumLo_MedM_v3_rr1_Sum.wav) |
| `VZ_BALA` | a balafon bar | 16 kHz, 110 ms | [EthnicXylo_hardM_C4_vl3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Balafon/Hard%20Mallet/EthnicXylo_hardM_C4_vl3_rr1_Mid.wav) |
| `VZ_SHAKER` | a shaker | 22.05 kHz, 90 ms | [Mid_ShakerHighFaster_Down_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Shaker%2C%20Small/Mid_ShakerHighFaster_Down_rr1.wav) |
| `VZ_AGOGO` | an agogo bell | 22.05 kHz, 70 ms | [Agogo_High_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Agogo%20Bells/Agogo_High_v3_rr1_Mid.wav) |
| `VZ_TUMBA` | a tumba, the low conga | 8 kHz, 160 ms | [Tumba_HitN_v4_rr1_Sum.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Conga/Tumba_HitN_v4_rr1_Sum.wav) |
| `VZ_QUINTO` | a quinto, the high conga | 11.025 kHz, 120 ms | [Quinto_HitN_v3_rr1_Sum.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Conga/Quinto_HitN_v3_rr1_Sum.wav) |
| `VZ_BONGO` | a bongo | 11.025 kHz, 100 ms | [BongoH_Hit1_v2_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Bongos/BongoH_Hit1_v2_rr1_Mid.wav) |
| `VZ_CAJON` | a cajon | 16 kHz, 100 ms | [Cajon_hit1_f_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Cajon/Cajon_hit1_f_rr1.wav) |
| `VZ_CLAP` | hands clapping | 16 kHz, 110 ms | [Clap_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Claps/Clap_rr1.wav) |
| `VZ_CABASA` | a cabasa | 22.05 kHz, 55 ms | [Cabasa1_Hit_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Cabasa/Cabasa1_Hit_rr1_Mid.wav) |
| `VZ_CLAVE` | claves | 22.05 kHz, 40 ms | [Claves1_Hit_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Claves/Claves1_Hit_v3_rr1_Mid.wav) |
| `VZ_COWBELL` | a cowbell | 16 kHz, 90 ms | [Cowbell2_Normal_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Cowbells/Cowbell2_Normal_v3_rr1_Mid.wav) |
| `VZ_TAMB` | a tambourine | 22.05 kHz, 100 ms | [Tamb1_Hit_v2_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Tambourine%201/Tamb1_Hit_v2_rr1_Mid.wav) |
| `VZ_TIMP` | a kettledrum | 11.025 kHz, 176 ms | [Timpani3_Hit_v3_rr2_Sum.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Timpani%201/Hit/Timpani3_Hit_v3_rr2_Sum.wav) |
| `VZ_BASSDRUM` | a concert bass drum | 5.512 kHz, 300 ms | [bassdrum_hit_ff.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Bass%20Drum%202/bassdrum_hit_ff.wav) |
| `VZ_SNARE` | a rope-tension snare drum | 16 kHz, 140 ms | [RopeSnare_hi_sn_Main_vl4_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Snare%20Drum%2C%20Rope%20Tension/Hi/RopeSnare_hi_sn_Main_vl4_rr1.wav) |
| `VZ_STICK` | the same drum's rim | 22.05 kHz, 45 ms | [RopeSnare_sidestick_Main_vl2_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Snare%20Drum%2C%20Rope%20Tension/RopeSnare_sidestick_Main_vl2_rr1.wav) |
| `VZ_CYMBAL` | a suspended cymbal | 16 kHz, 230 ms | [susCymb1_hit_fff1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Suspended%20Cymbal%201/susCymb1_hit_fff1.wav) |
| `VZ_TRIANGLE` | a triangle | 22.05 kHz, 46 ms | [Triangle3_HitM_v1_rr2_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Triangles/Triangle3_HitM_v1_rr2_Mid.wav) |
| `VZ_WOODBLOCK` | a woodblock | 22.05 kHz, 45 ms | [wood_click_ff.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Woodblock/wood_click_ff.wav) |
| `VZ_SLAP` | a slapstick | 22.05 kHz, 55 ms | [slapstick_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Slapstick/slapstick_rr1.wav) |
| `VZ_BRAKE` | a brake drum, hammered | 16 kHz, 90 ms | [BrakeDrum1_Hammer_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Brake%20Drum/BrakeDrum1_Hammer_v3_rr1_Mid.wav) |
| `VZ_ANVIL` | an anvil | 22.05 kHz, 65 ms | [Anvil_Hit2_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Anvil/Anvil_Hit2_v3_rr1_Mid.wav) |
| `VZ_HAT` | a hi-hat, closed | 22.05 kHz, 60 ms | [HiHat_HitC_v3_rr1_Mid.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Hi-Hat%20Cymbal/HiHat_HitC_v3_rr1_Mid.wav) |
| `VZ_GLOCK` | a glockenspiel bar | 22.05 kHz, 55 ms | [glock_loud_C5_01.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Glockenspiel/glock_loud_C5_01.wav) |
| `VZ_MBIRA` | a kalimba tine | 16 kHz, 76 ms | [Mbira6_Normal_MainSpirit_B3_k12_vl3_rr2.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Plucked%20Idiophones/Kalimba%2C%20Kenya/Mbira6_Normal_MainSpirit_B3_k12_vl3_rr2.wav) |
| `VZ_CHIME` | a hand chime | 16 kHz, 68 ms | [sus_A4_r01_main.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Hand%20Chimes/sus_A4_r01_main.wav) |
| `VZ_KICK` | a kick drum, snares off (kick mic) | 11.025 kHz, 220 ms | [kickmic_kick_snoff_vl3_rr1.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/kickmic/kick/kickmic_kick_snoff_vl3_rr1.flac) |
| `VZ_XSTICK` | a snare's cross-stick (snare mic) | 22.05 kHz, 70 ms | [snaremic_snare_crossstick_vl11.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/snaremic/snare/snaremic_snare_crossstick_vl11.flac) |
| `VZ_SNARE2` | a snare drum, centre (snare mic) | 22.05 kHz, 160 ms | [snaremic_snare_center_vl22.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/snaremic/snare/snaremic_snare_center_vl22.flac) |
| `VZ_TOMLO` | a low tom, hand-muted | 11.025 kHz, 160 ms | [mid_ltom_muted_vl10.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/mid/ltom/mid_ltom_muted_vl10.flac) |
| `VZ_TOMHI` | a high tom, centre | 11.025 kHz, 200 ms | [mid_htom_center_vl10.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/mid/htom/mid_htom_center_vl10.flac) |
| `VZ_OHAT` | a hi-hat, open | 22.05 kHz, 140 ms | [mid_hh_open_vl3_rr1.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/mid/hh/mid_hh_open_vl3_rr1.flac) |
| `VZ_RIDE` | a ride cymbal | 22.05 kHz, 190 ms | [mid_ride_ride_vl2_rr1.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/mid/ride/mid_ride_ride_vl2_rr1.flac) |
| `VZ_MSNARE` | a marching snare drum | 22.05 kHz, 130 ms | [snare3_f_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Legacy%20Snares/drum3_marching/snare3_f_rr1.wav) |
| `VZ_MRIM` | the same drum, a rimshot | 22.05 kHz, 60 ms | [snare3_rimshot_f_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Legacy%20Snares/drum3_marching/snare3_rimshot_f_rr1.wav) |
| `VZ_TENOR` | a tenor drum, the lower | 16 kHz, 160 ms | [tenor_f_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Legacy%20Toms/tenor_lower/tenor_f_rr1.wav) |
| `VZ_TENORH` | a tenor drum, the higher | 16 kHz, 160 ms | [tenorH_f_rr1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Membranophones/Struck%20Membranophones/Legacy%20Toms/tenor_higher/tenorH_f_rr1.wav) |
| `VZ_CLASHS` | clash cymbals, held short | 22.05 kHz, 160 ms | [cymbal_crash1_short1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Clash%20Cymbals%201/cymbal_crash1_short1.wav) |
| `VZ_CLASH` | clash cymbals, let ring | 22.05 kHz, 190 ms | [cymbal_crash2_f1.wav](https://raw.githubusercontent.com/sgossner/VCSL/master/Idiophones/Struck%20Idiophones/Clash%20Cymbals%202/cymbal_crash2_f1.wav) |
| `VZ_VHAT` | a hi-hat, closed | 22.05 kHz, 60 ms | [mid_hh_closed_vl3_rr1.flac](https://raw.githubusercontent.com/sfzinstruments/virtuosity_drums/9f04cf9a734527edfbb0a4eee1f674e45bbf71bc/Samples/mid/hh/mid_hh_closed_vl3_rr1.flac) |
