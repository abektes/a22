# Release checklist

What has to be true before a `vX.Y.Z` tag is pushed and A22 is submitted to
[The Firmware Hub](https://thefirmwarehub.com/submit/). The Hub lists a firmware only when it has
been installed on a real FM-1 from V15 and from another firmware, and left again.

Tick a box only after doing it on hardware. "Remote" below means done on a real FM-1 over USB with
`base/tools/fm1_remote.py` (keys, buttons and encoders pressed by the computer, read back through the
screen reader's channel, CC 119's PERF line and the crash history); what needs ears or hands says so.

## Done so far

**0.1.0** (2026-11-21, one unit, the flash chip unknown: A22 doesn't report it). Development builds were
installed over another Felucca-based firmware (`FM-1_727`), then over A22 itself, then the released
`a22-0.1.0.fwsc` from GitHub (its `SHA256SUMS` line and build attestation checked first). It answers
`FM-1_4122000100`. Not done for 0.1.0: section 1 (from stock V15), section 3 (leaving A22), section 5
(a unit with the other flash chip), section 6, and everything that needs listening.

## 0. Before touching an FM-1

- [ ] The design is signed off in the simulator (`dist/a22.rfm1`, or `./build.sh --host`).
- [x] CI is green on the commit to be released: the host tests, and the firmware job's build with
      JieLi's toolchain. Read the firmware job's log for the sizes: the app has to fit its flash
      slot, and the pool and `.data`/`.bss` have to fit RAM (see "RAM and flash on the device" in
      [ARCHITECTURE.md](ARCHITECTURE.md)). The build refuses when they don't.
      *0.1.0 (`a8e6363`): image 405,984 B of the 581,564 B slot; SRAM 440,176 of 475,136 B, 34,960 B free.*
- [x] Get the package to test: Actions, **Build and attest firmware**, **Run workflow** on that
      commit, then download the run's `packages` artifact. That is `a22.fwsc`, a development build
      with identity `FM-1_412200`.
- [x] Type `FM-1_4122` into the identity checker on the
      [Hub's submit page](https://thefirmwarehub.com/submit/) and confirm no other firmware uses
      it. If it is taken, change `id_prefix` in `app/fm1.toml` and build again.
      *2026-11-21: `FM-1_4122` and `FM-1_412200` both free.*
- [ ] Back up the test FM-1 with whatever it runs now. On Windows,
      [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) can save the whole flash.
- [ ] Know the way out before going in: read the Hub's
      [rescue page](https://thefirmwarehub.com/tools/rescue/) once.

## 1. Install from stock V15

- [x] Start from official V15 (the Hub's **Back to official V15** puts it there).
      *2026-11-23: M-VAVE's published `FM-1_015.fwsc` (SHA-256 `db1642b2…`, the file the Hub checks),
      installed with `fm1_install.py` because this browser could not grant Web MIDI. `--info` answered
      `FM-1_015` and the USB name was `FM-1`.*
- [x] Install `a22.fwsc`: `python3 base/tools/fm1_install.py a22.fwsc`. It finishes without an error
      and the FM-1 restarts into A22 (FM-4 comes up).
      *2026-11-23: the released `a22-0.4.0.fwsc` (SHA-256 `96eb3b58…`) from that V15. The installer
      finished the write; the screen (FM-4) was not looked at.*
- [x] `python3 base/tools/fm1_install.py --info` answers `FM-1_412200`.
      *2026-11-23: the release answers `FM-1_4122000400`.*
- [x] The computer shows the USB device as `A22 FM-1` (macOS may keep showing the MIDI port under a
      name it saw before, such as `Felucca`). *2026-11-23: USB name `A22 FM-1`, MIDI port still `Felucca`.*

## 2. It works

- [ ] Every one of the 27 keys sounds, and the right key lights. *(ears and eyes)*
- [ ] All seven encoders and fourteen buttons do what the README's hotkeys table says.
      *Remote, in part: PRESETS, ALGORITHM, K1 to K4, the pages, EDIT + ALGORITHM (sequencer mode),
      EDIT + ARP (hold), hold SAVE (two seconds reset CHIP to PULSE) and hold HOME all answered. Toasts
      are not on the screen reader's channel, so the rest needs eyes.*
- [ ] PRESETS goes through all fourteen engines. Each one draws, animates smoothly and sounds. Note any
      that stutter. *0.4.0, remote: 88 of its 96 algorithms with an eight-note chord held and PLATE on
      (MONO's eight skipped by the script's route, MONO seen on its own): 20 to 62 frames a second, the
      slowest FM-4 (20 to 24) and STACK CHOIR (20); no crash.*
- [ ] No crackle or dropouts with eight notes held on the heaviest engines (FM-4, SHAPER, HAZE) with
      PLATE and DELAY turned up. *(ears; 0.4.0, remote: the audio's busiest moment was 74% of its time
      over its 96 algorithms, so there is headroom)*
- [ ] MASTER goes from silent to full without clicks. Headphones and the speaker both play. The
      ring in the middle of the screen, marked GLOBAL, follows the knob and is gone a second after
      it stops; it does not come up by itself with the knob at rest.
- [ ] Hold EDIT: the ring comes up marked for the track and stays while EDIT is down. Turn SELECT or
      MASTER with it held: the track in hand gets quieter or louder without clicks, everything else
      stays as it was. Let go and turn MASTER away from where it was: nothing jumps. A quick tap of
      EDIT pins the knob strip.
- [ ] Tape: REC arms, the first key starts the take, REC closes it and it plays on; REC while it plays
      overdubs; PLAY stops and starts. Hold REC clears the track in hand; held three seconds it asks
      CLEAR ALL?, and only a tap of REC then clears every track (from the release after 0.1.0). The
      question stays on screen until answered; let go, wait three seconds, and it says TAPE KEPT; let go
      and turn an encoder or play a key, and a tap of REC records instead of clearing.
- [ ] Tracks: EDIT + PRESETS goes T1 to T4, each keeps its engine and algorithm; EDIT + SEL mutes the
      track in hand; the MIX page's volume, pan and send work per track.
- [ ] Step: hold SEQ swaps TAPE and STEP. REC writes, PLAY runs the pattern, a tap of EDIT is a rest,
      eight keys held together land on one step and it plays all eight. The PATTERN page shows each
      track's lane.
- [ ] RIFF and SHAKE (EDIT + ALGORITHM): a riff records and plays from any key; SHAKE's balls play
      their notes and, once they bounce out, nothing keeps sounding.
- [ ] 0.4.0's sound changes, by ear: WAVE's eight presets (SPREAD 24 to 46, now narrower: raise it if they
      lost their width); DELAY at 120 BPM 1/4 (the right echo at 750 ms, not a comb); PLATE brought back
      after a bypass starts silent, with no click or gap in the sound (it clears 48 KB in one block).
- [ ] DICE: EDIT + LFO rolls the four knobs of the algorithm in hand, and none of them runs away or goes
      silent; EDIT + ENV goes back to the knobs from before the roll, and again to the roll. A roll while
      the tape records is on the tape. *(ears for "runs away")*
- [ ] Settings saved by 0.4.0 (version 4) load with everything kept: install 0.4.0, change an engine, an
      algorithm, its knobs and an effect, save; install this release over it: they are all as they were.
- [ ] Saving waits for silence: no stall heard in a reverb tail or under the metronome; SAVE says SAVE
      WHEN QUIET while something plays, and the setting is there after a restart.
- [ ] PR #2 under load: HAZE with two tape tracks and eight keys. CC 119's PERF says LATE and SHED, no
      full-level static at a low MASTER, and the screen, keys and MASTER keep answering.
- [ ] The SEQ page (abektes/a22#1): KNOB 1 turns through every mode, into SHAKE and out again. In SHAKE,
      MODE GRAV SPIN BOUNCE, then SELECT for MODE CAGE; SHAKE turned on again opens on the first. PLAY KEYS
      TO DROP sits inside the smallest cage (CAGE 0). In the simulator, the ALGORITHM slider there is SEQ MODE.
- [ ] Metronome: hold GLO cycles CLICK REC, CLICK ON, CLICK OFF, and holding never opens the page.
- [x] MIDI in over USB plays notes, and CC 20 to 23 move the four knobs. *Remote: CC 20 turned CHIP's
      DUTY to 16 and 79; eight MIDI notes held raised the audio's load from 25% to 39%.*
- [x] MIDI out over USB sends the keys', the arpeggiator's and the tape's notes. *Remote: a latched
      arpeggio's notes arrived on the computer.*
- [ ] USB audio: the computer records the mix from the `A22 FM-1` input. *(macOS asks for microphone
      access first)*
- [ ] Change the engine, an algorithm and a knob, wait ten seconds, switch off and on: they
      are kept. *Remote, in part: an algorithm was kept through a reinstall's restart; a power-off
      needs hands.*
- [ ] Turn a knob, turn ALGORITHM and back: each algorithm has its knobs where they were left.
      Hold SAVE says the algorithm's name and RESET, and its four knobs are as new. Held two
      seconds it says the engine's name and RESET. Held four seconds, ALL RESET.
- [ ] Snapshots: SAVE + a white key saves to an empty one and loads a full one; SAVE + EDIT + key saves
      over it.
- [ ] USB host: hold OCT − and OCT + with a USB-MIDI keyboard on a powered hub; it plays, and the same
      again goes back to the computer.
- [x] Leave it running for thirty minutes. No freeze, no SAFE MODE. *Remote, 2026-11-21: STACK CLOUD
      with PLATE and a four-note arpeggio latched for 30 minutes, about 2,260 notes every five minutes
      out over MIDI: 31 frames a second and the audio at 51% (55% at its busiest) at every reading, no
      drift; afterwards 63 frames and 10%, as before it began; no crash recorded.*

## 3. Leave A22 again

- [x] From A22, install official V15 (the Hub's **Back to official V15**). The FM-1 runs stock
      again and still works. *2026-11-23: from `FM-1_412200` to `FM-1_015` (USB name `FM-1`).
      Identity and USB name checked; not listened to.*
- [ ] From A22, install another custom firmware from the Hub. It starts.

## 4. Install from another firmware

- [x] From another custom firmware, install `a22.fwsc`. It starts, and `--info` answers
      `FM-1_412200`. *November 2026: from `FM-1_727`, a Felucca-based firmware.*
- [x] Install `a22.fwsc` over A22 itself (an update). Settings are kept. *2026-11-21: 0.1.0 over 0.1.0
      kept CHIP on ARP. An update that changes the saved format (`H1_VERSION`) starts the settings
      fresh by design, the tape kept: say so in its release notes.*

## 5. A newer-flash unit

The FM-1s sold since autumn 2026 carry a UC25HQ80 flash (JEDEC `0xB36014`) in place of the Puya
P25Q80 (`0x856014`). A firmware that only knows the Puya can't save or update on them. fm1-base, and
so A22 and its update loader, accept both (`base/tests/flash_ok_test.c`; `firmware.json` lists both).

- [ ] Do sections 1, 3 and 4 on a newer unit. A22 doesn't report the flash ID over USB, so the test
      is the next line: on a chip a firmware doesn't know, settings are gone after a power cycle.
- [ ] On that unit, settings survive a power cycle (this is the part that fails on a firmware that
      doesn't know the chip).
- [ ] If only one kind of unit is at hand, find a tester with the other kind before submitting,
      and say in the release notes which kind was tested by whom.

## 6. Recovery

- [ ] Hold REC while switching on: the FM-1 enters the chip's update mode, and switching off and
      on again starts A22 normally.

## 7. Release

- [x] The README's Install section points at the releases (no "no release yet" note).
- [x] The repository topics: `m-vave`, `fm-1`, `firmware`, `synthesizer`.
- [x] Merge to `main`. `fmhub.json` has to be in the root of the default branch.
- [x] The top of `CHANGELOG.md` and `dist/a22.rfm1` name the version (`tools/build_rfm1.sh X.Y.Z`):
      the release workflow refuses a tag otherwise.
- [x] Tag: `git tag vX.Y.Z && git push a22 vX.Y.Z` (the remote is abektes/a22; a local `v0.x` tag
      may be h1's: push `<commit>:refs/tags/vX.Y.Z` then). The workflow builds `a22-X.Y.Z.fwsc`
      (identity `FM-1_4122XXYYZZ`: v0.1.0 is `FM-1_4122000100`), tests, attests and publishes the
      release. *v0.1.0: https://github.com/abektes/a22/releases/tag/v0.1.0*
- [x] Install the released `a22-X.Y.Z.fwsc` once on the FM-1, as a last check of the exact file.
- [x] Submit at [thefirmwarehub.com/submit](https://thefirmwarehub.com/submit/) (only once sections 1,
      3 and 4 are done: the Hub asks how it was tested, from which firmware and back to which).
      *2026-11-23: sent as Ahmet Bektes. The Hub's check was all green (source, licence, fmhub.json,
      sounds, 0.4.0, `FM-1_4122000400`, Felucca loader, both flash chips) and the card went live.
      Leaving A22 for another custom firmware on the Hub was not part of this pass.*
