/* The device's MIDI out queue (app/app.c, midi_send): what may still go when the USB queue is nearly full. Run by
 * tests/run_tests.sh. The host simulator's queue never fills, so the rule is checked here on its own:
 *   a note-off, a CC 123 (any channel), Start, Stop    while there is any room (a note left out is held for
 *                                                       ever; a Start left out and the DAW never runs);
 *   the clock                                          while more than MIDI_CLOCK_SPARE places are left;
 *   a note-on, a knob                                  while more than MIDI_SPARE places are left. */
#include <stdio.h>
#include <stdint.h>
#include "../app/midi_tiers.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static int goes(uint8_t status, uint8_t d1, uint32_t room) { return room > midi_out_need(status, d1); }

int main(void)
{
    int ch;
    for (ch = 0; ch < 4; ch++) {
        CHECK(goes((uint8_t)(0x80 | ch), 60, 1), "a note-off goes with one place left, on every channel");
        CHECK(goes((uint8_t)(0xB0 | ch), 123, 1), "a CC 123 goes with one place left, on every channel");
        CHECK(!goes((uint8_t)(0x90 | ch), 60, MIDI_SPARE), "a note-on waits for more than MIDI_SPARE places");
        CHECK(goes((uint8_t)(0x90 | ch), 60, MIDI_SPARE + 1), "and goes with one more");
        CHECK(!goes((uint8_t)(0xB0 | ch), 20, MIDI_SPARE), "a knob waits for more than MIDI_SPARE places");
    }
    CHECK(goes(0xFA, 0, 1), "Start goes with one place left");
    CHECK(goes(0xFC, 0, 1), "Stop goes with one place left");
    CHECK(!goes(0xF8, 0, MIDI_CLOCK_SPARE), "a clock waits for more than MIDI_CLOCK_SPARE places");
    CHECK(goes(0xF8, 0, MIDI_CLOCK_SPARE + 1), "and goes with one more");
    CHECK(MIDI_CLOCK_SPARE > 0 && MIDI_CLOCK_SPARE < MIDI_SPARE, "the clock between the offs and the notes");
    CHECK(!goes(0x80, 60, 0), "nothing goes with no room (nobody listening)");
    printf("miditiers: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
