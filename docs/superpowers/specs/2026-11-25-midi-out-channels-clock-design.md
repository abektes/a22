# MIDI out: a channel per track, and MIDI clock

Short design, agreed 2026-11-25. Firmware only, no new screen, no change to the saved state.

## Why

Firmware reviews on the FM-1 keep marking grooveboxes down for "no MIDI export: play it and record it".
A22 already sends its notes over USB MIDI, so recording it in a DAW works, but badly: all four tape tracks
arrive mixed on channel 1, and the DAW has no idea of A22's tempo or where bar 1 is. This makes "record A22
into the DAW" work properly: four separate parts, each on its own channel, recorded in time with the DAW's bars.

## Goals

- Each tape track goes out on its own channel: T1 on channel 1, T2 on 2, T3 on 3, T4 on 4.
- MIDI clock (24 a beat) at TEMPO, with Start and Stop, so a DAW set to external sync follows A22.
- Recorded knob moves (the tape's automation) go out as CCs, so the DAW gets them along with the notes.

## Non-goals

- The DAW driving A22: no clock in, no following the DAW's PLAY, no playing T1–T4 from channels 1–4. MIDI in
  stays as it is (any channel plays the track in hand).
- A .mid file export (a later idea: a SysEx dump of the tape, turned into a file by a web page).
- MIDI out over the TRS jack (it is an input on the FM-1).

## Anti-goals

- Notes left stuck in the DAW. A note-off always goes out on the channel its note-on went on, whatever has
  changed in between (the track in hand, the arp mode, the tape stopping).
- A clock that wobbles. A DAW following A22 must not lose clock messages when the USB queue is busy.
- A new setting, page or hidden combo. The reviewer's most common complaint is opaque UI.

## Approaches

- **A. Always on, no setting (picked).** Small and firmware only. It costs anyone driving a single synth from
  A22: T2–T4 go quiet unless that synth listens on all channels. A22's MIDI out goes over USB to a computer, so
  the other end is nearly always a DAW. A setting can come later if someone asks.
- B. The same, plus a MIDI subpage on GLO (CHANNEL: CH1 / PER TRACK, CLOCK: ON / OFF). Turned down: a fourth GLO
  subpage, a version bump and migration for the saved settings, and more to explain, to protect a rare setup.
- C. A toggle on a button combo. Turned down: a hidden switch, the very thing the reviewer marks firmwares
  down for.

## Behaviour

**Channels.**
- Tape notes: their own track's channel (`tape_fire_ev`, from the event's `trk`).
- Step sequencer notes: each note's own track (`step_fire`, `st.step_trk`, and `step_st` for the offs).
- Live keys, chords, the arpeggiator, Riff and Shake: the channel of the track in hand when the note started.
  The note-off goes on that same channel even if the track in hand changed while it was held.
- Knob CCs (CC 20 to 23, `set_knob` in `core/ui.c`): the track in hand's channel.
- Tape automation as it plays back: CC 20 to 23 on the track's channel, 0..100 scaled to 0..127 as the knobs
  already are.
- Hold HOME (all notes off): CC 123 on channels 1 to 4, not just 1.
- Nothing changes for T1: playing the keys on T1 sends exactly what it sends today.

**Clock.**
- 0xF8 every 4 of the sequencer's ticks (96 a beat, so 24 a beat), counted in `block()` from the same tick
  count the tape and step sequencer use. One control block is 32 samples, so a clock is at most 0.73 ms late.
- Clock runs all the time, stopped or playing, so a DAW can lock to the tempo before anything starts.
- It follows TEMPO, not the tape's SPEED: at 0.5X, 2X or REV the notes go out as heard and the clock stays
  at TEMPO. Swing doesn't touch the clock.
- Start (0xFA) when the tape or the step pattern goes from stopped to running: PLAY, or the first key of a
  take. Stop (0xFC) when it stops: PLAY again, hold HOME, swapping TAPE and STEP. Start isn't sent again
  each time the loop goes round: the DAW keeps counting bars.
- No Song Position Pointer. A loop whose IN bar isn't 1 still sends Start at IN.

**Getting through.** `midi_send` (`app/app.c:285`) drops a note-on or a CC when the USB queue has 16 or fewer
free places, and keeps the last places for note-offs. Clock, Start and Stop get through ahead of notes and CCs, in
tiers (changed after review): a note-off, CC 123, Start and Stop while there is any room; the clock while more
than 8 places are left; a note-on or a CC while more than 16 are (`app/midi_tiers.h`, `tests/miditiers.c`). (A clock at 48 to 96 a second sharing the note-offs'
last places would fill them within a fraction of a second of a stall, and the offs could no longer go.) With nothing listening there is no room and nothing is sent, as today.
The USB-MIDI packet for a one-byte real-time message is code index 0xF, which `midi_send`'s packing already
gives for 0xF8, 0xFA and 0xFC.

## Left to the builder

- Where the clock's tick count and the transport's running/stopped edge are kept (`beat_out` and
  `loop_state` / the step sequencer's state are nearby in `core/synth.c`).
- How the channel of a live note is remembered for its note-off (for example a 128-entry table set at
  note-on).
- Tests in `tests/` for: the channel of each source; a key held across a track change; CC 123 on four
  channels; clock count per bar at a couple of tempos; Start and Stop on each transport change; clock still
  sent while the queue is nearly full.
- The README's "USB audio and MIDI" section and the CHANGELOG.

## Changed after the code review (2026-11-25)

- Start is in the queue's top tier (above).
- No clock, Start or Stop while the port is USB host: a keyboard with a sequencer would follow them.
- A knob set over MIDI and recorded is marked on the tape (flag bit 3, kept through save and load) and not sent as
  the tape plays it: "nothing that came in goes back".
- The tape jumping back to IN while it plays sends Stop and Start.
- `h1_midi_resync()`: a new USB connection, or the port back from USB host, gets Stop and Start when the tape or
  step pattern next comes round to its start, the clock's phase where the tape is.
- Not changed: the clock's phase still jumps at a Start (one interval of 0 to 4 ticks before it), because the clock
  runs while stopped and a Start falls where the player presses.
