/* MIDI out's queue tiers (app.c, midi_send), apart so tests/miditiers.c can check them: how many places must still
 * be free in the USB queue for a message to go. What may be left out goes in three tiers, so a computer that stops
 * reading for a moment never loses what matters most to the room the rest took:
 *   a note-off, a CC 123, Start,     any room (a note left out is held for ever; a Start left out and the DAW
 *   Stop                             never runs: the core sends it once a start);
 *   the clock                        more than MIDI_CLOCK_SPARE (48 to 96 clocks a second would fill the last places
 *                                    within a fraction of a second of a stall, and the offs could no longer go);
 *   a note-on, a knob                more than MIDI_SPARE. */
#ifndef A22_MIDI_TIERS_H
#define A22_MIDI_TIERS_H
#include <stdint.h>

#define MIDI_SPARE 16u
#define MIDI_CLOCK_SPARE 8u

static inline uint32_t midi_out_need(uint8_t status, uint8_t d1)
{
    if (status == 0xFAu || status == 0xFCu || (status & 0xF0u) == 0x80u || ((status & 0xF0u) == 0xB0u && d1 == 123u))
        return 0u;
    if (status >= 0xF8u)
        return MIDI_CLOCK_SPARE;
    return MIDI_SPARE;
}

#endif
