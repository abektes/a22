/* h1 - what the core's files share. Not for ports. */
#pragma once
#include "h1.h"
#include "mathx.h"
#include "gfx.h"

#define NVOICE 8
#define NSTEP 64                 /* the step sequencer's pattern, at most */
#define NINST 14
#define NALGO 8
#define CTRL 32                  /* frames per control block: envelopes, LFO, coefficients */
#define KS_LEN 1024              /* a string's delay line: down to 43 Hz at 44.1 kHz */
#define SCOPE_N 512

/* big zeroed buffers: the device puts them in its .pool RAM (fm1-base PLAT_POOL) */
#ifdef H1_DEVICE
#define H1_POOL __attribute__((section(".pool")))
#else
#define H1_POOL
#endif

/* ---- voices --------------------------------------------------------------------------- */
typedef struct Voice {
    uint8_t stage;               /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    uint8_t gate;
    uint8_t note;
    uint8_t idx;
    uint8_t part;                /* the instrument and algorithm it sounds with (PART), 0 = never used */
    uint8_t seq;                 /* 1: the sequencer started it (LAYERS), and the sequencer lets it go */
    uint32_t serial;             /* counts note-ons: the picture sees a new note when it changes */
    uint32_t stamp;              /* allocation order */
    f32 pitch;                   /* MIDI note, gliding, before bends */
    f32 target;
    f32 env, env_prev;
    f32 age;                     /* seconds since the note began */
    f32 pan;                     /* -1..1 */
    f32 r1, r2;                  /* two random numbers per note */
    f32 ph[6];                   /* oscillator phases, in turns */
    f32 s[14];                   /* the instrument's own state */
    uint32_t u[3];
} Voice;

/* ---- saved parameters ----------------------------------------------------------------- */
typedef struct InstParams {
    uint8_t knob[4];             /* 0..100: the four live modulations of the home screen */
    uint8_t algo;
    uint8_t env[4];              /* attack, decay, sustain, release: 0..100 */
    uint8_t lfo[4];              /* shape, rate, depth, target */
    uint8_t fx[4];               /* echo, time, space, tone */
    uint8_t arp[4];              /* mode, rate, range, gate */
    uint8_t rsv[3];
} InstParams;

#define H1_MAGIC 0x31483148u     /* "H1H1" */
#define H1_VERSION 5u
#define H1_VERSION_4_SIZE (sizeof(H1State) - NSTEP * 4u)   /* version 4 ended before step_part */

typedef struct H1State {
    uint32_t magic;
    uint8_t version;
    uint8_t inst;
    int8_t octave;               /* -2..2 */
    uint8_t hud;                 /* the knob strip pinned on the home screen */
    uint8_t keys[4];             /* root, scale, glide, hold */
    uint8_t glo[4];              /* tempo, swing, tune, bright */
    uint8_t seq[8];              /* mode (loop, step), bars, meter, loop snap, step grid, click, layers */
    InstParams ip[NINST];
    uint8_t ink[NINST][3];
    uint8_t rsv[5];
    uint8_t steps[NSTEP][4];     /* the step pattern: up to four notes a step, note + 1, 0 = none */
    uint8_t step_part[NSTEP][4]; /* the part each of those notes was played on (PART), 0 = none kept */
} H1State;

/* A part: an instrument and one of its algorithms, in a byte. What a recorded note remembers, so that
 * with LAYERS on it goes on sounding as it was played when another instrument is in hand. */
#define PART(inst, algo) ((uint8_t)(0x80u | ((uint32_t)(inst) << 3) | ((uint32_t)(algo) & 7u)))
#define PART_INST(p) ((((p) >> 3) & 15) < NINST ? (((p) >> 3) & 15) : 0)
#define PART_ALGO(p) ((p) & 7)

extern H1State st;

/* ---- what an instrument's DSP gets every control block -------------------------------- */
typedef struct Blk {
    f32 k[4];                    /* the four knobs, 0..1: smoothed, with the LFO on its target */
    int algo;
    f32 sr, isr;
    f32 lfo;                     /* -1..1 */
    f32 bend;                    /* semitones added to every voice (tune, LFO on pitch) */
    f32 time;                    /* seconds since boot */
    f32 bpm;
    f32 step;                    /* seconds the instrument's own clocks move on in this call: a control block's, or
                                  * 0 when its block() is called again in the same block for another part */
} Blk;

/* ---- what an instrument's picture gets every frame ------------------------------------ */
typedef struct VVoice {
    uint8_t on, gate, note;
    uint32_t serial;
    f32 pitch, env, age;
} VVoice;

typedef struct Vis {
    f32 t, dt;                   /* seconds; dt is clamped */
    f32 k[4];                    /* the knobs as heard (with the LFO), eased for the eye */
    int algo, algo_prev;
    f32 algo_t;                  /* 0..1 across an algorithm change */
    f32 enter;                   /* seconds since this instrument came up */
    int fresh;                   /* 1 on the first frame after it came up */
    VVoice v[NVOICE];
    f32 level;                   /* output level, 0..1 */
    f32 lfo;
    f32 beat;                    /* 0..1 within the beat */
    uint32_t notes;              /* note-ons so far, all voices */
    f32 last_pitch;              /* of the newest note */
    const f32 *scope;            /* the last SCOPE_N mono samples, scope_pos = the oldest */
    int scope_pos;
    uint32_t *rng;
} Vis;

typedef struct InstDef {
    const char *name;
    uint8_t ink[3];
    const char *knob[4];
    const char *algo[NALGO];
    InstParams def;
    f32 gain;
    void (*block)(const Blk *b);
    void (*note)(Voice *v, const Blk *b);
    void (*render)(Voice *v, const Blk *b, f32 *out, int n);
    void (*post)(const Blk *b, f32 *l, f32 *r, int n);
    void (*draw)(const Vis *vis);
} InstDef;

extern const InstDef inst_pip, inst_bonk, inst_urchin, inst_twang, inst_lava, inst_knot, inst_ink, inst_tide, inst_haunt,
                     inst_bard, inst_turbo, inst_orbit, inst_cozy, inst_koi;
extern const InstDef *const insts[NINST];

/* ---- settings pages ------------------------------------------------------------------- */
enum { PG_HOME, PG_FX, PG_KEYS, PG_ENV, PG_LFO, PG_GLO, PG_ARP, PG_SEQ, NPAGE };
enum { PK_PCT, PK_ENUM, PK_INT };

typedef struct ParamDef {
    const char *name;
    uint8_t kind;
    uint8_t max;                 /* PCT: 100. ENUM: options - 1. INT: max */
    uint8_t min;
    int8_t show_off;             /* INT: added to the stored value when shown */
    const char *const *opts;
} ParamDef;

typedef struct PageDef {
    const char *title;
    ParamDef p[4];
} PageDef;

extern const PageDef pages[NPAGE];
const ParamDef *page_param(int page, int i);     /* (the SEQ page's last column depends on its mode) */
uint8_t *page_value(int page, int i);

/* ---- engine (synth.c): everything below runs in the audio interrupt, except where noted - */
enum { EV_NOTE_ON = 1, EV_NOTE_OFF, EV_PANIC, EV_INST, EV_UNLATCH, EV_REC, EV_PLAY,
       EV_CLEAR,                 /* a = 0: everything recorded; a = 1 + an instrument: only what was played on it */
       EV_STEP_MOVE,             /* the step cursor: a = detents + 128 */
       EV_STEP_REST,             /* empty the step under the cursor and move on */
       EV_SEQ_MODE };            /* loop <-> step: stop whichever was running */
enum { LOOP_EMPTY, LOOP_IDLE, LOOP_ARMED, LOOP_REC, LOOP_PLAY, LOOP_DUB };
enum { SEQ_LOOP, SEQ_STEP };
enum { CLICK_REC, CLICK_ON, CLICK_OFF };       /* the metronome: while recording, always, never */

/* what the screen needs to know about the sequencer */
typedef struct SeqInfo {
    int mode;                    /* SEQ_LOOP or SEQ_STEP */
    int loop_state;
    f32 loop_pos;                /* 0..1 */
    int playing, write;          /* step mode */
    int nsteps, per_bar, bars;
    int cursor, at;              /* the step being edited, the step sounding */
} SeqInfo;
void eng_seq_info(SeqInfo *si);
int seq_bar_ticks(void);                         /* 96 ticks to the quarter note */
int seq_grid_ticks(void);
int seq_accent(int tick_in_bar);                 /* 2 the bar's first beat, 1 another beat, 0 between */

void eng_init(uint32_t sample_rate);
void eng_post(int type, int a);                  /* main loop -> audio, lock-free */
void eng_render(int32_t *lr, uint32_t n);
void eng_snapshot(Vis *vis);                     /* main loop: copy what the picture needs */
uint32_t eng_sounding_lo(void);                  /* notes sounding, as key bits for the LEDs */
int eng_seq_leds(uint32_t *out, int page_open, int blink);   /* the pattern on the white keys; 0: not showing */
uint32_t eng_voices(void);
int eng_seq_has(int inst);                       /* main loop: something recorded was played on this instrument */
extern Voice voices[NVOICE];
extern f32 ks_line[NVOICE][2][KS_LEN];
extern uint32_t eng_rng;

/* scale helper shared by UI and engine */
int scale_snap(int note);

/* ui.c */
void ui_init(void);
void ui_frame(uint32_t now_ms);
int ui_page(void);
