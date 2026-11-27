/* A22 - a firmware for the M-VAVE FM-1, derived from h1 (whose names the calls keep). The public face of the
 * portable core.
 *
 * The core knows nothing about hardware. A port feeds it the panel (keys, buttons, encoder
 * detents), asks it for audio in the audio interrupt and for a frame in the main loop, and
 * sends the dirty parts of the framebuffer to the panel. Three ports share it unchanged:
 * port/rfm1 (the build the site's simulator, web/, and github.com/jakehandy/fm1-browser-simulator run),
 * port/host (tests: WAV and image dumps) and app/ (the device, on fm1-base). */
#pragma once
#include <stdint.h>

#define H1_W 240
#define H1_H 240
#define H1_NKEYS 27              /* F3 .. G5 */

/* the panel, in fm1-base's order (plat.h B_* and EN_*) so the device port is a pass-through */
enum { H1_BTN_FX, H1_BTN_SEL, H1_BTN_ENV, H1_BTN_LFO, H1_BTN_EDIT, H1_BTN_GLO, H1_BTN_HOME, H1_BTN_SAVE,
       H1_BTN_ARP, H1_BTN_SEQ, H1_BTN_PLAY, H1_BTN_REC, H1_BTN_OCTDN, H1_BTN_OCTUP, H1_NBTN };
enum { H1_ENC_SELECT, H1_ENC_ALGO, H1_ENC_PRESET, H1_ENC_K1, H1_ENC_K2, H1_ENC_K3, H1_ENC_K4, H1_NENC };

void h1_init(uint32_t sample_rate);

/* main loop side */
void h1_key(int key, int down);                  /* key 0 = F3 */
void h1_button(int btn, int down);
void h1_encoder(int enc, int detents);           /* + = clockwise */
void h1_midi(uint8_t status, uint8_t d1, uint8_t d2);   /* MIDI in: notes with their velocity, CC 20-23 the
                                                  * knobs, CC 123 all notes off */
/* MIDI out: fn is called with each message h1 sends, each track on its own channel (T1 on 1 to T4 on 4): the
 * notes played by the tape and the step sequencer on their track's channel (as they sound: a KEY-on track's
 * notes moved to the key in force), those of the keys and the arpeggiator
 * on the track in hand's (note-on with its velocity, its note-off on the same channel); CC 20-23 as the four home
 * knobs turn (the track in hand's channel) and as the tape plays its automation back (its track's); and CC 123 on
 * channels 1 to 4 on a hush. MIDI clock (0xF8, 24 a beat) at TEMPO all the time, from inside h1_render, with
 * Start (0xFA) and Stop (0xFC) as the tape or the step pattern starts and stops, and Stop then Start as the tape
 * jumps back to IN while it plays; none of the three while h1_usb_host. Never what came in over MIDI (a knob set
 * over MIDI and recorded is not sent as the tape plays it). The notes are sent from inside h1_render, the
 * knobs from h1_encoder (the main loop): fn must be safe from both. NULL: nothing is sent (the default). */
void h1_midi_out(void (*fn)(uint8_t status, uint8_t d1, uint8_t d2));
/* A new listener on MIDI out (a computer just connected, or the port back from USB host): if the tape or the step
 * pattern is playing, it is sent Stop and Start when that next comes round to its start, with the clock in step
 * from there, so it can follow from bar 1. Safe from the main loop. */
void h1_midi_resync(void);
void h1_master(int level);                       /* where the volume knob stands, 0..1000, every frame. The
                                                  * core shows it (a ring while it turns) and, with EDIT held,
                                                  * takes it as the volume of the instrument in hand */
int h1_global_volume(void);                      /* The port owns MASTER's gain and asks this first: -1, the
                                                  * gain follows the knob; 0..1000, it is held there (the knob
                                                  * was last the instrument's and has not come back through
                                                  * it). Safe to read from the audio side. */
void h1_frame(uint32_t now_ms);                  /* advance the animation and draw the frame */

/* audio interrupt side: n stereo frames, signed 24-bit in int32 (fm1-base's app_render format) */
void h1_render(int32_t *lr, uint32_t n);
/* Stems, for a USB audio input with 8 channels of the port's own beside the mix: get gives the frame off frames into
 * the render being made (8 int16s, the next frame stride on, for *run frames), or NULL while nobody records. h1_render
 * then writes the four tracks there, T1 on the first pair to T4 on the fourth, at the mix's scale before MASTER: each
 * after its volume and pan, dry (no effect, master bus, punch or metronome: those run over the whole mix), with the
 * limiter's gain as the mix has it. The mix itself is the same to the bit with or without. While get gives NULL, none
 * of it runs. Set it before the audio starts. */
void h1_stems(int16_t *(*get)(uint32_t off, uint32_t *run), uint32_t stride);
/* Whether the last audio half went out late (rendered after its time): call once a render, from where h1_render is
 * called. Two late halves in a row and a voice lets go fast (10 ms), one at a time, a note the player is not holding
 * first, so the sound gets back in time instead of running late half after half (which starves the main loop).
 * h1_voices_shed: how many so far. */
void h1_late(int late);
uint32_t h1_voices_shed(void);

/* Where an audio half's time goes, for a port's timing readout. clock is a free-running count the port has (the FM-1's
 * 24 MHz timer); with it set, each h1_render adds up the counts spent in each stage below. h1_stages gives, since the
 * last call, the stages of the render that took longest in all (the one closest to running late), and its total;
 * then starts again. Call it from the main loop with the audio held off for the moment it reads; the clock may be set
 * at any time from the main loop (A22's port sets it at the first CC 119). NULL (the default):
 * nothing is timed and h1_stages gives zeros.
 *   SEQ     events from the UI, the tape, the step sequencer, the arpeggiator
 *   VOICES  the voices this core renders
 *   WAIT    waiting for the second core's voices (or rendering them here, when it gave them back)
 *   MIX     each voice's gain, declick and pan into its track, and the tracks into the mix and the send
 *   FX      the effect in hand
 *   MASTER  the master bus, the tape tricks, the meters, the limiter and the output */
enum { H1_STG_SEQ, H1_STG_VOICES, H1_STG_WAIT, H1_STG_MIX, H1_STG_FX, H1_STG_MASTER, H1_NSTG };
void h1_stage_clock(uint32_t (*clock)(void));
uint32_t h1_stages(uint32_t out[H1_NSTG]);       /* returns the worst render's total count */

/* A second core, where a port has one. run hands it fn(arg) and returns 1, or returns 0 (then the core
 * that asked does the work); wait returns 0 when the job is back, anything else if it was dropped (then
 * too). h1_render gives it every other voice and does the rest meanwhile, so run and wait are called
 * from wherever h1_render is. The sound is the same to the bit with or without. Call it before the
 * audio starts; never is fine. */
void h1_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));

/* the picture: 240 x 240 intensities, and the ink ramp that colours them (RGB565, 256 entries) */
const uint8_t *h1_framebuffer(void);
const uint16_t *h1_palette(void);

/* presenting: calls blit once per changed rectangle (in screen order) and returns the bytes a
 * 16-bit panel would be sent, the number that decides the frame rate on the device */
typedef void (*h1_blit_fn)(int x, int y, int w, int h);
uint32_t h1_present(h1_blit_fn blit);
/* The same in pieces, for a panel that is slow to take its pixels (the FM-1's: a full screen is 21 ms):
 * begin() finds what changed and queues it, next() hands out one rectangle at a time, and the pixels
 * of a queued rectangle are read from h1_panel(), which the next h1_frame does not touch. So the queue
 * can go out while the next frame is drawn: h1_while_drawing(fn) has fn called many times a frame from
 * inside the drawing, and fn sends a rectangle when the panel is ready for one. Empty the queue before
 * the next begin(). */
void h1_present_begin(void);
int h1_present_next(int *x, int *y, int *w, int *h);   /* 0: the queue is empty */
int h1_present_left(void);                       /* rectangles still queued */
int h1_present_rects(void);                      /* rectangles the last begin() queued */
uint32_t h1_present_bytes(void);                 /* what the last begin() queued costs to send, in bytes */
const uint8_t *h1_panel(void);                   /* the picture as the panel has it once the queue is out */
void h1_while_drawing(void (*fn)(void));

/* lights */
uint32_t h1_key_leds(void);                      /* bit k = key k */
uint32_t h1_button_leds(void);                   /* bit b = H1_BTN_b */

/* saved state: one blob. dirty() says it changed and may be written (then call saved()). */
const void *h1_state(uint32_t *size);
int h1_state_load(const void *blob, uint32_t size);   /* 0 = taken */
int h1_state_dirty(void);
void h1_state_saved(void);
int h1_state_asked(void);                        /* 1 once after SAVE: write the state now, silent or not (then 0) */
/* The loop recorder's loop, as a blob of its own (at most H1_LOOP_MAX bytes: 2,048 events, a note-on
 * and a note-off a note), so that what was recorded is there
 * after a power-off. Load it after h1_state_load (which empties the loop). dirty() is 1 when it changed and no take
 * is going into it, whether it plays or not; then h1_loop gives it, and saved() takes the dirt off what was given.
 * h1_loop is the main loop's, and may be called while the loop plays: it only reads what is recorded. The blob
 * is the engine's own memory, not a copy, and stays a loadable loop until a take records again. */
const void *h1_loop(uint32_t *size);
int h1_loop_load(const void *blob, uint32_t size);   /* 0 = taken; anything else leaves the loop empty */
int h1_loop_dirty(void);
int h1_quiet(void);                              /* 1: silent enough for a flash erase (nothing playing, tails gone) */
int h1_loop_quiet(void);                         /* 1: the loop is stopped (or empty), no take going */
int h1_loop_taking(void);                        /* 1: a take is going in (REC or an overdub) */
/* Projects (docs/PLAN-projects.md): a saved state and loop in place of what plays now. h1_silence stops the tape and
 * every voice as hold HOME does (CC 123 out on the four channels); once h1_quiet, h1_project_load takes the state and
 * the loop with the render locked out (the caller's: plat_lock), as a start does. TUNE and BRIGHT stay the unit's.
 * Both then count as changed, so they are saved as the state and loop that play. 0 = taken; -1: the state was
 * refused and nothing changed. A loop that does not load leaves the tape empty. */
void h1_silence(void);
int h1_project_load(const void *state, uint32_t state_size, const void *loop, uint32_t loop_size);
/* The panel's side: SAVE + F#, G#, A# or C# (the first four black keys) asks to load project 0..3, with EDIT held to
 * save. h1_proj_take gives the ask once (0 none, 1 save, 2 load) and its project; the port says each frame which
 * projects hold a record (bit p) and which is being saved (-1 none), and how an ask ended. h1_proj_said gives the
 * words of a project toast once ("" none), for a screen reader. */
int h1_proj_take(int *p);
void h1_proj_news(uint32_t have, int saving);
void h1_proj_done(int kind, int p, int ok);
const char *h1_proj_said(void);
void h1_loop_saved(void);
#define H1_LOOP_MAX 16396u                       /* 12 + 8 x 2048 */
#define H1_STATE_MAX 3584u                      /* the saved state's most: a project's slot holds it and a full loop */
uint32_t h1_loop_gen(void);                     /* moves whenever what is recorded changes */
int h1_loop_room(void);                          /* notes that still fit in the loop (0: full) */

/* USB host: OCT- and OCT+ held together for a second turn the USB port into a host for a USB-MIDI keyboard,
 * and again back to the computer. The core keeps the choice (not saved: every start is a device) and calls
 * fn(on) from h1_frame; the port switches the port and tells the core what it found with h1_toast. With no
 * fn the hold says NO USB HOST. h1_usb_host: 1 while the core has asked for host. */
void h1_usb_host_fn(void (*fn)(int on));
int h1_usb_host(void);
void h1_toast(const char *text);                 /* a word from the port, shown as a toast (upper case, 16 at most) */

/* introspection for the simulator and tests */
int h1_instrument(void);
int h1_algorithm(void);                          /* of the current engine, 0..7 */
int h1_page(void);                               /* 0 = home, else a settings page */
int h1_fx_held(void);                            /* 1 while FX is held on its own (the PUNCH strip is up) */
int h1_punch(void);                              /* the punch the hand holds, 1-8, else 0 */
const char *h1_punch_name(int p);                /* "BRAKE", "ST 1/8" .. "GATE" ("" for none) */
const char *h1_toast_now(void);                  /* the toast showing, "" when none */
int h1_clear_all_asked(void);                    /* 1 while CLEAR ALL? waits for its answer */
int h1_clear_all_answer(void);                   /* its answer, once: 1 kept, 2 cleared, else 0 */
int h1_layers(void);                             /* always 1 in A22: recorded notes keep their engine (h1's LAYERS) */
int h1_instrument_volume(int inst);              /* 0..100: the volume of the track in hand (inst is not used) */
int h1_volume_ring(void);                        /* the volume ring: 0 not up, 1 GLOBAL (MASTER), 2 INSTRUMENT */
int h1_instrument_count(void);
const char *h1_instrument_name(int i);
const char *h1_knob_name(int inst, int k);       /* the four home-screen modulations */
const char *h1_algo_name(int inst, int a);       /* eight per engine; "" past the last */
void h1_ink(int inst, uint8_t rgb[3]);
void h1_set_ink(int inst, uint8_t r, uint8_t g, uint8_t b);
uint32_t h1_voices_active(void);

/* Every encoder as a number: returns what it is set to right now and (where the pointers are not
 * null) its range, the name of what it is tied to and its value as text. The simulator draws its
 * sliders from this; set() moves a control there outright, the way a slider wants. */
int h1_control(int enc, int *lo, int *hi, const char **name, const char **text);
void h1_control_set(int enc, int value);
