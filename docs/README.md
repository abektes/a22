# A22's docs

The [README](../README.md) is the manual and the install guide. The rest:

**Current**
- [ARCHITECTURE.md](ARCHITECTURE.md): how A22 is built: the core and its ports, the two cores, the picture, an
  engine, the tape, the saved state, RAM and flash on the device, and the rules the FM-1 enforces.
- [RELEASE-CHECKLIST.md](RELEASE-CHECKLIST.md): what has to be true before a tag, with what each release was checked
  doing.
- [SAMPLES.md](SAMPLES.md) and [WAVETABLES.md](WAVETABLES.md): every recording A22 carries, and where it came from.
- [../CHANGELOG.md](../CHANGELOG.md): each release, with what it was measured doing on an FM-1.
- [../PRODUCT.md](../PRODUCT.md) and [../DESIGN.md](../DESIGN.md): the website's audience and its design system.

**Records** (plans and research as they were written; later releases may have changed what they describe)
- [PLAN-projects.md](PLAN-projects.md): next, four projects (tape and settings) kept in flash, and project files over
  USB (issue #12).
- [PLAN-0.7.2.md](PLAN-0.7.2.md): 0.7.2, the newer fm1-base, the sliced erase, denormal speed and the engines in RAM,
  with the FM-1's numbers.
- [PLAN-cpu-headroom.md](PLAN-cpu-headroom.md): 0.6.0's CPU work for loop jams.
- [PLAN-expansion-sprints.md](PLAN-expansion-sprints.md) and [PLAN-after-sprints.md](PLAN-after-sprints.md): the six
  sprints after 0.4.0 (DICE, CYCLE, SKINS, PERC's modes) and how they became 0.5.0.
- [RESEARCH-cpu-ram.md](RESEARCH-cpu-ram.md): CPU and RAM tactics for the FM-1, from the SDK and other firmwares.
- [RESEARCH.md](RESEARCH.md): h1's notes on the FM-1 and what makes firmware for it good.
- [SPIKE-wavetable-presets-chords.md](SPIKE-wavetable-presets-chords.md): the research before WAVE, factory presets
  and chord memory.
- [superpowers/](superpowers/): the specs and plans for 0.7.0's MIDI out, KEY FOLLOW, PUNCH and LANE.
