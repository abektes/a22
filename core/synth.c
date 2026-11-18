/* h1 - the engine: voices, envelopes, LFO, arpeggiator, the sequencer (a loop recorder and a step
 * sequencer, one at a time) and the block loop that
 * calls the current instrument. Runs in the audio interrupt; the main loop talks to it through a
 * small lock-free queue (eng_post) and reads back plain floats for the picture. */
#include "internal.h"
#include "dsp.h"

void fx_init(f32 sr);
void fx_block(f32 *l, f32 *r, const InstParams *ip, f32 bpm);

f32 sin_tab[SIN_N + 1];
H1State st;
Voice voices[NVOICE];
f32 ks_line[NVOICE][2][KS_LEN] H1_POOL;
uint32_t eng_rng = 0x2F6E2B1u;

const InstDef *const insts[NINST] = {&inst_cozy, &inst_orbit, &inst_koi, &inst_haunt, &inst_tide,
                                     &inst_pip, &inst_bonk, &inst_urchin, &inst_twang, &inst_lava,
                                     &inst_knot, &inst_ink, &inst_bard, &inst_turbo};

static f32 SR = 44100.0f, ISR = 1.0f / 44100.0f;
static const InstDef *cur;
static int cur_i;
static volatile int pending_i = -1;
static int fade_blocks;
static Blk blk;                                  /* the numbers of the instrument in hand, for the picture */

/* What the engine keeps for each instrument while it sounds. With LAYERS on, notes that were recorded on
 * other instruments go on sounding beside the one in hand, so every one of them needs its own eased
 * knobs, LFO and envelope rates. */
typedef struct Lfo { f32 ph, sh, drift; } Lfo;
typedef struct IRun {
    Blk b;                                       /* its numbers for this control block */
    f32 knob_s[4];                               /* the knobs of the algorithm in hand, eased */
    f32 kmod;                                    /* what the LFO adds to a knob, and which (-1: none) */
    int kmod_k;
    Lfo lf;                                      /* its own LFO, while it is not the one in hand */
    f32 vol, panmod;
    f32 ig;                                      /* its own volume (INST_VOL) as a gain, eased: what the mix takes of it */
    f32 att, kd, sus, kr;
    f32 last_pitch;
    uint32_t at;                                 /* the control block b is for */
    uint32_t adv;                                /* the control block its own clocks last moved in */
    uint16_t linger;                             /* control blocks its post still runs after its last voice */
    uint8_t owner;                               /* the part its block() was last called for */
} IRun;
#define LINGER 400                               /* 0.3 s */
static IRun ir[NINST];
static uint32_t blkno = 16;
static Lfo lfo_live;                             /* the LFO of the instrument in hand: one clock across instruments */
static uint32_t lfo_rng = 0x1F0C1F0Cu;
static int lay_was;
static f32 run_time;
static uint32_t stamp_ctr;
static f32 last_pitch = 60.0f;
static volatile uint32_t note_count;
static volatile f32 level_out;
static volatile f32 lfo_out;
static volatile f32 beat_out;

static f32 scope[SCOPE_N];
static volatile uint32_t scope_w;

/* ---- the queue from the main loop ------------------------------------------------------ */
static volatile uint32_t evq[64];
static volatile uint32_t ev_w, ev_r;

void eng_post(int type, int a)
{
    uint32_t w = ev_w, nx = (w + 1u) & 63u;
    if (nx == ev_r)
        return;                                  /* full: drop (64 events inside one block) */
    evq[w] = ((uint32_t)type << 8) | ((uint32_t)a & 255u);
    ev_w = nx;
}

void dsp_init(void)
{
    int i;
    for (i = 0; i <= SIN_N; i++)
        sin_tab[i] = sin_turns((f32)i / (f32)SIN_N);
}

/* ---- scales --------------------------------------------------------------------------- */
static const uint16_t SCALE_MASK[] = {
    0xFFF,      /* CHROMA */
    0xAB5,      /* MAJOR   1 0 1 0 1 1 0 1 0 1 0 1 */
    0x5AD,      /* MINOR   1 0 1 1 0 1 0 1 1 0 1 0 */
    0x295,      /* PENTA   major pentatonic */
    0x4A9,      /* MINPENT minor pentatonic */
    0x4E9,      /* BLUES */
    0x6AD,      /* DORIAN */
    0x555,      /* WHOLE */
};

int scale_snap(int note)
{
    uint32_t mask = SCALE_MASK[st.keys[1] < 8 ? st.keys[1] : 0];
    int root = st.keys[0] % 12, i;
    for (i = 0; i < 12; i++) {
        int pc = ((note - i - root) % 12 + 12) % 12;
        if (mask & (1u << pc))
            return note - i;
    }
    return note;
}

/* ---- voices --------------------------------------------------------------------------- */
static int layers(void) { return st.seq[6] == 1; }
static uint8_t live_part(void) { return PART(cur_i, st.ip[cur_i].algo < NALGO ? st.ip[cur_i].algo : 0); }
static const Blk *part_set(int part);

/* A note starts, on a part: an instrument and one of its algorithms. A voice that last sounded another
 * instrument is emptied first: what an instrument keeps in a voice means nothing to the next. */
static void voice_on_part(int note, int part, int seq)
{
    Voice *v = 0;
    const Blk *b;
    int i, j, inst = PART_INST(part);
    if (note < 0 || note > 127)
        return;
    for (i = 0; i < NVOICE && !v; i++)
        if (voices[i].stage && voices[i].gate && voices[i].note == note && voices[i].part == part)
            v = &voices[i];
    for (i = 0; i < NVOICE && !v; i++)
        if (!voices[i].stage)
            v = &voices[i];
    if (!v) {
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (!voices[i].gate && voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    if (!v) {
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    if (v->part && PART_INST(v->part) != inst) {
        uint32_t serial = v->serial;
        uint8_t idx = v->idx, *p = (uint8_t *)v;
        for (j = 0; j < (int)sizeof(Voice); j++)
            p[j] = 0;
        v->serial = serial;
        v->idx = idx;
        for (j = 0; j < KS_LEN; j++)
            ks_line[idx][0][j] = ks_line[idx][1][j] = 0.0f;
    }
    v->part = (uint8_t)part;
    v->seq = (uint8_t)seq;
    v->target = (f32)note;
    if (inst == cur_i) {
        v->pitch = st.keys[2] ? last_pitch : (f32)note;
        last_pitch = (f32)note;
    } else                                       /* (a glide is from the instrument's own last note) */
        v->pitch = st.keys[2] && ir[inst].last_pitch > 0.0f ? ir[inst].last_pitch : (f32)note;
    ir[inst].last_pitch = (f32)note;
    v->note = (uint8_t)note;
    v->gate = 1;
    v->stage = 1;
    v->age = 0.0f;
    v->stamp = ++stamp_ctr;
    v->pan = clampf(((f32)note - 64.0f) * (1.0f / 40.0f), -0.45f, 0.45f);
    v->r1 = rng_f(&eng_rng);
    v->r2 = rng_f(&eng_rng);
    b = part_set(part);
    insts[inst]->note(v, b);
    v->serial++;
    if (inst == cur_i)
        note_count++;                            /* (the picture's count: the notes of the instrument it shows) */
}

static void voice_off_part(int note, int part)   /* the sequencer lets go of a note it started */
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && voices[i].seq && voices[i].note == note && voices[i].part == part) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void voice_on(int note) { voice_on_part(note, live_part(), 0); }

/* a note played by hand, or by the arpeggiator, ends: whatever algorithm it began on */
static void voice_off(int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && !voices[i].seq && voices[i].note == note && PART_INST(voices[i].part) == cur_i) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void voices_release_all(void)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

/* ---- arpeggiator ---------------------------------------------------------------------- */
static uint8_t held[16];
static int nheld;
static int arp_i, arp_stepn;
static f32 arp_acc = 1e9f, arp_off;
static uint8_t arp_cur[16];
static int arp_ncur;
static int arp_was;
static const f32 RATE_TICKS[6] = {96.0f, 48.0f, 32.0f, 24.0f, 16.0f, 12.0f};

static void arp_take(int note, int on);          /* (the loop recorder, below) */

static void arp_release(void)
{
    int i;
    for (i = 0; i < arp_ncur; i++) {
        voice_off(arp_cur[i]);
        arp_take(arp_cur[i], 0);
    }
    arp_ncur = 0;
}

static void arp_fire(const InstParams *ip, f32 len)
{
    uint8_t s[16];
    int n = nheld, i, j, range = iclamp(ip->arp[2], 1, 4), total, idx = 0, mode = ip->arp[0];
    f32 gate = (f32)ip->arp[3] * 0.01f;
    arp_release();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        s[i] = held[i];
    if (mode != 5)
        for (i = 1; i < n; i++) {                /* sort ascending */
            uint8_t x = s[i];
            for (j = i; j > 0 && s[j - 1] > x; j--)
                s[j] = s[j - 1];
            s[j] = x;
        }
    total = n * range;
    if (mode == 6) {                             /* CHORD: everything held, octave walking */
        int oct = arp_i % range;
        for (i = 0; i < n; i++) {
            int note = s[i] + 12 * oct;
            if (note <= 127) {
                voice_on(note);
                arp_take(note, 1);
                arp_cur[arp_ncur++] = (uint8_t)note;
            }
        }
        arp_i++;
    } else {
        int note;
        switch (mode) {
        case 2: idx = total - 1 - (arp_i % total); break;
        case 3: {
            int period = total > 1 ? 2 * total - 2 : 1, p = arp_i % period;
            idx = p < total ? p : period - p;
            break;
        }
        case 4: idx = (int)(rng_next(&eng_rng) % (uint32_t)total); break;
        default: idx = arp_i % total; break;
        }
        arp_i++;
        note = s[idx % n] + 12 * (idx / n);
        if (note <= 127) {
            voice_on(note);
            arp_take(note, 1);
            arp_cur[arp_ncur++] = (uint8_t)note;
        }
    }
    arp_off = gate >= 0.995f ? 1e9f : len * maxf(0.06f, gate);
}

static void arp_tick(const InstParams *ip, f32 dticks)
{
    f32 step, sw, len;
    if (ip->arp[0] == 0) {
        if (arp_was) {
            arp_release();
            arp_was = 0;
            nheld = 0;
        }
        return;
    }
    arp_was = 1;
    if (arp_ncur) {
        arp_off -= dticks;
        if (arp_off <= 0.0f)
            arp_release();
    }
    if (!nheld) {
        arp_acc = 1e9f;
        arp_i = 0;
        return;
    }
    step = RATE_TICKS[ip->arp[1] < 6 ? ip->arp[1] : 3];
    sw = (f32)st.glo[1] * 0.01f * 0.34f;
    len = ((arp_stepn - 1) & 1) ? step * (1.0f - sw) : step * (1.0f + sw);
    arp_acc += dticks;
    if (arp_acc >= len) {
        if (arp_acc > 1e8f) {
            arp_acc = 0.0f;
            arp_stepn = 0;
        } else
            arp_acc -= len;
        arp_fire(ip, (arp_stepn & 1) ? step * (1.0f - sw) : step * (1.0f + sw));
        arp_stepn++;
    }
}

/* a note after the latch and the loop: to the arpeggiator when it runs, else to a voice */
static void route(int note, int on)
{
    int i;
    if (st.ip[cur_i].arp[0] == 0) {
        if (on) voice_on(note);
        else voice_off(note);
        return;
    }
    if (on) {
        for (i = 0; i < nheld; i++)
            if (held[i] == note)
                return;
        if (nheld < 16)
            held[nheld++] = (uint8_t)note;
    } else {
        for (i = 0; i < nheld; i++)
            if (held[i] == note) {
                for (; i + 1 < nheld; i++)
                    held[i] = held[i + 1];
                nheld--;
                break;
            }
    }
}

/* ---- the sequencer's clock: bars, meters, grids -------------------------------------------- */
static const uint16_t METER_TICKS[5] = {384, 288, 480, 288, 336};      /* 4/4 3/4 5/4 6/8 7/8 */
static const uint8_t GRID_TICKS[3] = {48, 24, 12};                     /* 1/8 1/16 1/32 */

int seq_bar_ticks(void) { return METER_TICKS[st.seq[2] < 5 ? st.seq[2] : 0]; }
int seq_grid_ticks(void) { return GRID_TICKS[st.seq[4] < 3 ? st.seq[4] : 1]; }

int seq_accent(int tick)
{
    if (tick == 0)
        return 2;
    switch (st.seq[2]) {
    case 3: return tick == 144;                              /* 6/8: two groups of three */
    case 4: return tick == 96 || tick == 192;                /* 7/8: 2 + 2 + 3 */
    default: return tick % 96 == 0;
    }
}

static int step_per_bar(void) { return seq_bar_ticks() / seq_grid_ticks(); }
static int step_bars(void)
{
    int bars = 1 << (st.seq[1] & 3), pb = step_per_bar();
    while (bars > 1 && bars * pb > NSTEP)
        bars >>= 1;
    return bars;
}
static int step_count(void) { return imin(NSTEP, step_bars() * step_per_bar()); }

/* ---- loop recorder -------------------------------------------------------------------- */
typedef struct LEv { uint16_t tick; uint8_t note; uint8_t on; uint8_t part, rsv; } LEv;   /* on: bit 0 = on, bit 7 = recorded this pass; part: what it was played on */
#define NLEV 384
static LEv lev[NLEV];
static int nlev;
static volatile int loop_state = LOOP_EMPTY;
static volatile f32 loop_pos, loop_len = 768.0f;
static uint32_t loop_held[4];
static f32 click_env, click_ph, click_ph2, click_f, click_pe, click_lp, click_amp;
static uint32_t click_rng = 0x7407C0DEu;
static int click_at = -1;
static f32 free_pos;                             /* the metronome's own bar, when nothing is running under it */
static int free_on;

/* ---- the metronome: a soft knock on the meter's beats, a little higher on the one ------------- */
static int click_mode(void) { return st.seq[5] < 3 ? st.seq[5] : CLICK_REC; }

static void click_run(f32 pos, int sound)
{
    int bt = seq_bar_ticks(), tick = (int)pos % bt, e = (int)pos / 24;
    if (e != click_at && sound && seq_accent(tick - tick % 24)) {
        click_env = 1.0f;
        click_pe = 1.0f;
        click_ph = click_ph2 = 0.0f;
        click_f = tick < 24 ? 392.0f : 294.0f;
        click_amp = tick < 24 ? 0.3f : 0.23f;
    }
    click_at = e;
}
static int loop_fresh;                           /* the take about to start replaces the loop */

static void loop_release(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (loop_held[n >> 5] & (1u << (n & 31)))
            route(n, 0);
    loop_held[0] = loop_held[1] = loop_held[2] = loop_held[3] = 0;
    for (n = 0; n < NVOICE; n++)                 /* and with LAYERS, the voices it started itself */
        if (voices[n].stage && voices[n].gate && voices[n].seq) {
            voices[n].gate = 0;
            voices[n].stage = 4;
        }
}

static void loop_play_range(f32 a, f32 b)
{
    int i;
    for (i = 0; i < nlev; i++) {
        f32 t = (f32)lev[i].tick;
        if (t >= a && t < b && !(lev[i].on & 0x80)) {
            int n = lev[i].note;
            if (layers()) {                      /* each note on the part it was played on, past the arpeggiator */
                int part = lev[i].part ? lev[i].part : live_part();
                if (lev[i].on & 1)
                    voice_on_part(n, part, 1);
                else
                    voice_off_part(n, part);
            } else if (lev[i].on & 1) {
                loop_held[n >> 5] |= 1u << (n & 31);
                route(n, 1);
            } else if (loop_held[n >> 5] & (1u << (n & 31))) {
                loop_held[n >> 5] &= ~(1u << (n & 31));
                route(n, 0);
            }
        }
    }
}

static void loop_tick(f32 dticks)
{
    f32 a, b;
    int i, s = loop_state;
    if (s != LOOP_REC && s != LOOP_PLAY && s != LOOP_DUB)
        return;
    a = loop_pos;
    b = a + dticks;
    if (s != LOOP_REC)
        loop_play_range(a, b);
    click_run(b, s == LOOP_PLAY ? click_mode() == CLICK_ON : click_mode() != CLICK_OFF);
    if (b >= loop_len) {
        b -= loop_len;
        loop_release();
        for (i = 0; i < nlev; i++)
            lev[i].on &= 0x7F;
        if (s == LOOP_REC)
            loop_state = LOOP_PLAY;
        click_at = -1;
        loop_play_range(0.0f, b);
    }
    loop_pos = b;
}

static void loop_record(int note, int on)
{
    static const f32 Q[3] = {0.0f, 24.0f, 48.0f};
    f32 t = loop_pos, q = Q[st.seq[3] < 3 ? st.seq[3] : 0];
    if (nlev >= NLEV)
        return;
    if (on && q > 0.0f) {
        t = (f32)ifloor(t / q + 0.5f) * q;
        if (t >= loop_len)
            t -= loop_len;
    }
    lev[nlev].tick = (uint16_t)t;
    lev[nlev].note = (uint8_t)note;
    lev[nlev].on = (uint8_t)((on ? 1 : 0) | 0x80);
    lev[nlev].part = live_part();
    lev[nlev].rsv = 0;
    nlev++;
}

/* With LAYERS a take is played back note for note, past the arpeggiator, so what goes into it is what
 * the arpeggiator played, not the keys that were held. */
static int arp_recorded(void) { return layers() && st.ip[cur_i].arp[0] != 0; }
static void arp_take(int note, int on)
{
    if (arp_recorded() && st.seq[0] != SEQ_STEP && (loop_state == LOOP_REC || loop_state == LOOP_DUB))
        loop_record(note, on);
}

static void loop_stop(void)
{
    loop_release();
    loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY;
    loop_pos = 0.0f;
}

/* ---- step sequencer -------------------------------------------------------------------------
 * A row of steps, st.steps, up to four notes each. Stopped with WRITE on, a key (or a chord)
 * fills the step under the cursor and the cursor moves on when the hand lifts; running with
 * WRITE on, what is played lands on the nearest step. */
static volatile int step_playing, step_write, step_cursor, step_at;
static f32 step_acc, step_off;
static uint8_t step_snd[NSNOTE], step_sp[NSNOTE];         /* the notes the step holds, and the part of each (0: through route) */
static int step_nsnd, step_gesture;

static void step_release(void)
{
    int i;
    for (i = 0; i < step_nsnd; i++) {
        if (step_sp[i])
            voice_off_part(step_snd[i], step_sp[i]);
        else
            route(step_snd[i], 0);
    }
    step_nsnd = 0;
}

static void step_fire(int i, f32 len)
{
    int j;
    step_release();
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j]) {
            int n = st.steps[i][j] - 1, part = 0;
            if (layers()) {
                part = st.step_part[i][j] & 0x80 ? st.step_part[i][j] : live_part();
                voice_on_part(n, part, 1);
            } else
                route(n, 1);
            step_snd[step_nsnd] = (uint8_t)n;
            step_sp[step_nsnd++] = (uint8_t)part;
        }
    step_off = len;
}

/* everything the sequencer holds, however it started it */
static void seq_release_all(void)
{
    loop_release();
    step_release();
}

static f32 step_len(int i)
{
    f32 g = (f32)seq_grid_ticks(), sw = st.seq[4] < 2 ? (f32)st.glo[1] * 0.01f * 0.34f : 0.0f;
    return (i & 1) ? g * (1.0f - sw) : g * (1.0f + sw);
}

static void step_stop(void)
{
    step_release();
    step_playing = 0;
    step_at = 0;
    step_acc = 0.0f;
}

static void step_tick(f32 dticks)
{
    int n = step_count();
    if (step_nsnd) {
        step_off -= dticks;
        if (step_off <= 0.0f)
            step_release();
    }
    if (step_cursor >= n)
        step_cursor = 0;
    if (!step_playing)
        return;
    step_acc += dticks;
    if (step_at >= n || step_acc >= step_len(step_at)) {
        int at = step_at;
        step_acc = at >= n ? 0.0f : step_acc - step_len(at);
        at = at + 1 >= n ? 0 : at + 1;
        if (at == 0) click_at = -1;
        step_at = at;
        step_fire(at, step_len(at) * 0.85f);
    }
    click_run((f32)(step_at * seq_grid_ticks()) + step_acc, click_mode() == CLICK_ON);
}

/* with nothing playing, the metronome keeps its own bar: while a take is armed, or always if it is ON */
static void free_tick(f32 dticks)
{
    int cm = click_mode(), s = loop_state, run;
    if (st.seq[0] == SEQ_STEP)
        run = !step_playing && cm == CLICK_ON;
    else
        run = (s == LOOP_ARMED && cm != CLICK_OFF) || ((s == LOOP_EMPTY || s == LOOP_IDLE) && cm == CLICK_ON);
    if (!run) {
        free_on = 0;
        return;
    }
    if (!free_on) {
        free_on = 1;
        free_pos = 0.0f;
        click_at = -1;
    } else {
        free_pos += dticks;
        if (free_pos >= (f32)seq_bar_ticks()) {
            free_pos -= (f32)seq_bar_ticks();
            click_at = -1;
        }
    }
    click_run(free_pos, 1);
}

static void step_put(int i, int note)
{
    int j, part = live_part();
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j] == note + 1 && (!layers() || st.step_part[i][j] == part))
            return;
    for (j = 0; j < NSNOTE - 1 && st.steps[i][j]; j++)
        ;
    st.steps[i][j] = (uint8_t)(note + 1);
    st.step_part[i][j] = (uint8_t)part;
}

static void step_clear(int i)
{
    int j;
    for (j = 0; j < NSNOTE; j++)
        st.steps[i][j] = st.step_part[i][j] = 0;
}

/* a note's instrument: the one it was played on, or with none kept, the one in hand */
static int tag_inst(int part) { return part & 0x80 ? PART_INST(part) : cur_i; }

/* only the notes of one instrument out of a step: the others close up */
static void step_clear_inst(int i, int inst)
{
    int j, k = 0;
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j] && tag_inst(st.step_part[i][j]) != inst) {
            st.steps[i][k] = st.steps[i][j];
            st.step_part[i][k] = st.step_part[i][j];
            k++;
        }
    for (; k < NSNOTE; k++)
        st.steps[i][k] = st.step_part[i][k] = 0;
}

/* a new chord on a step, or a rest: with LAYERS only the instrument in hand gives up its place there */
static void step_empty(int i)
{
    if (layers())
        step_clear_inst(i, cur_i);
    else
        step_clear(i);
}

static void step_record(int note, int on)
{
    int n = step_count();
    if (step_playing) {
        if (on) {
            int i = step_at;
            if (step_acc > step_len(i) * 0.5f)
                i = i + 1 >= n ? 0 : i + 1;
            step_put(i, note);
        }
        return;
    }
    if (on) {
        if (step_gesture++ == 0)
            step_empty(step_cursor);
        step_put(step_cursor, note);
    } else if (step_gesture > 0 && --step_gesture == 0)
        step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
}

/* ---- live notes: the latch, then the recorder, then route -------------------------------- */
static uint32_t latched[4];
static int phys_down;

static void eff_note(int note, int on)
{
    if (st.seq[0] == SEQ_STEP) {
        if (step_write)
            step_record(note, on);
        route(note, on);
        return;
    }
    if (on && loop_state == LOOP_ARMED && loop_fresh && nlev && layers()) {
        int i;                                   /* LAYERS: a take over a stopped loop joins it from the top, */
        for (i = 0; i < nlev; i++)               /* and nothing is erased (holding REC is what clears) */
            lev[i].on &= 0x7F;
        loop_pos = 0.0f;
        click_at = -1;
        loop_state = LOOP_DUB;
        arp_acc = 1e9f;
    }
    if (on && loop_state == LOOP_ARMED) {
        if (loop_fresh)
            nlev = 0;
        loop_len = (f32)((1 << (st.seq[1] & 3)) * seq_bar_ticks());
        loop_pos = 0.0f;
        click_at = -1;
        if (free_on && free_pos < (f32)(seq_bar_ticks() - 48)) {
            loop_pos = free_pos;                 /* the click was counting: the take starts on its bar, not on your first note */
            click_at = (int)free_pos / 24;
        }
        loop_state = LOOP_REC;
        arp_acc = 1e9f;
    }
    if ((loop_state == LOOP_REC || loop_state == LOOP_DUB) && !arp_recorded())
        loop_record(note, on);
    route(note, on);
}

static void unlatch(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (latched[n >> 5] & (1u << (n & 31)))
            eff_note(n, 0);
    latched[0] = latched[1] = latched[2] = latched[3] = 0;
}

static void live_note(int note, int on)
{
    if (!st.keys[3]) {
        eff_note(note, on);
        return;
    }
    if (on) {
        if (phys_down == 0)
            unlatch();                           /* a new gesture replaces what was held */
        phys_down++;
        if (!(latched[note >> 5] & (1u << (note & 31)))) {
            latched[note >> 5] |= 1u << (note & 31);
            eff_note(note, 1);
        }
    } else if (phys_down > 0)
        phys_down--;
}

static void panic(void)
{
    int i;
    unlatch();
    loop_release();
    step_release();
    step_gesture = 0;
    nheld = 0;
    arp_release();
    phys_down = 0;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void handle(uint32_t e)
{
    int type = (int)(e >> 8), a = (int)(e & 255u);
    switch (type) {
    case EV_NOTE_ON: live_note(a & 127, 1); break;
    case EV_NOTE_OFF: live_note(a & 127, 0); break;
    case EV_PANIC: panic(); break;
    case EV_UNLATCH: unlatch(); phys_down = 0; break;
    case EV_INST:
        if (a < NINST && a != cur_i) {
            unlatch();
            nheld = 0;
            arp_release();
            if (layers()) {                      /* at once: what sounds keeps its own instrument, and a take goes on */
                int i;
                for (i = 0; i < NVOICE; i++)
                    if (voices[i].stage && voices[i].gate && !voices[i].seq) {
                        voices[i].gate = 0;
                        voices[i].stage = 4;
                    }
                cur_i = a;
                cur = insts[a];
                break;
            }
            pending_i = a;
            fade_blocks = 0;
            voices_release_all();
        }
        break;
    case EV_SEQ_MODE:
        loop_stop();
        step_stop();
        step_write = 0;
        step_gesture = 0;
        break;
    case EV_STEP_MOVE: {
        int n = step_count(), c = ((step_cursor + a - 128) % n + n) % n;
        step_cursor = c;
        step_gesture = 0;
        if (!step_playing)
            step_fire(c, 30.0f);                 /* hear what is there */
        break;
    }
    case EV_STEP_REST:
        if (step_playing)
            step_empty(step_at);
        else {
            int n = step_count();
            step_empty(step_cursor);
            step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
        }
        break;
    case EV_REC:
        if (st.seq[0] == SEQ_STEP) {
            step_write = !step_write;
            step_gesture = 0;
            break;
        }
        switch (loop_state) {
        case LOOP_EMPTY:
        case LOOP_IDLE:
            loop_fresh = 1;                      /* a new take; REC while it plays adds to it instead */
            loop_state = LOOP_ARMED;
            break;
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        case LOOP_REC: loop_state = LOOP_PLAY; break;
        case LOOP_PLAY: loop_state = LOOP_DUB; break;
        default: loop_state = LOOP_PLAY; break;
        }
        break;
    case EV_PLAY:
        if (st.seq[0] == SEQ_STEP) {
            if (step_playing)
                step_stop();
            else {
                step_playing = 1;
                step_at = 0;
                step_acc = 0.0f;
                click_at = -1;
                arp_acc = 1e9f;
                step_fire(0, step_len(0) * 0.85f);
            }
            break;
        }
        switch (loop_state) {
        case LOOP_EMPTY: break;
        case LOOP_IDLE: {
            int i;
            for (i = 0; i < nlev; i++)
                lev[i].on &= 0x7F;
            loop_pos = 0.0f;
            click_at = -1;
            arp_acc = 1e9f;
            loop_state = LOOP_PLAY;
            break;
        }
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        default: loop_stop(); break;
        }
        break;
    case EV_CLEAR:
        if (a > 0 && a <= NINST) {               /* only what was played on one instrument */
            int i, k = 0, inst = a - 1;
            seq_release_all();
            if (st.seq[0] == SEQ_STEP) {
                for (i = 0; i < NSTEP; i++)
                    step_clear_inst(i, inst);
                break;
            }
            for (i = 0; i < nlev; i++)
                if (tag_inst(lev[i].part) != inst)
                    lev[k++] = lev[i];
            nlev = k;
            if (!nlev) {
                loop_pos = 0.0f;
                loop_state = LOOP_EMPTY;
            }
            break;
        }
        if (st.seq[0] == SEQ_STEP) {
            int i;
            step_stop();
            for (i = 0; i < NSTEP; i++)
                step_clear(i);
            step_cursor = 0;
            break;
        }
        loop_release();
        nlev = 0;
        loop_pos = 0.0f;
        loop_state = LOOP_EMPTY;
        break;
    default: break;
    }
}

void eng_seq_info(SeqInfo *si)
{
    si->mode = st.seq[0] == SEQ_STEP ? SEQ_STEP : SEQ_LOOP;
    si->loop_state = loop_state;
    si->loop_pos = loop_len > 0.0f ? loop_pos / loop_len : 0.0f;
    si->playing = step_playing;
    si->write = step_write;
    si->per_bar = step_per_bar();
    si->bars = step_bars();
    si->nsteps = step_count();
    si->cursor = imin(step_cursor, si->nsteps - 1);
    si->at = imin(step_at, si->nsteps - 1);
}

/* The sequencer on the keys, as the stock firmware shows it: the sixteen white keys are the pattern from
 * left to right. One key a step up to sixteen steps, two steps a key up to thirty-two, and so on up. A key
 * is lit where its steps hold notes, and the one being played is turned over (a lit one goes dark, a dark
 * one lights), so the place runs across the keyboard. The loop is shown the same way, in sixteenths.
 * Returns 0 when the keys are not the sequencer's (then they show what is sounding). */
int eng_seq_leds(uint32_t *out, int page_open, int blink)
{
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    uint32_t m = 0;
    int n, per, at = -1, wr = -1, i;
    if (st.seq[0] == SEQ_STEP) {
        if (!step_playing && !step_write && !page_open)
            return 0;
        n = step_count();
        per = (n + 15) / 16;
        for (i = 0; i < n; i++)
            if (st.steps[i][0])
                m |= 1u << WHITE[i / per];
        if (step_playing)
            at = imin(step_at, n - 1) / per;
        else if (step_write)
            wr = imin(step_cursor, n - 1) / per;
    } else {
        int s = loop_state, ne = nlev;
        if (s != LOOP_REC && s != LOOP_PLAY && s != LOOP_DUB && !(page_open && s == LOOP_IDLE))
            return 0;
        n = imax(1, (int)loop_len / 24);
        per = (n + 15) / 16;
        for (i = 0; i < ne; i++)
            if (lev[i].on & 1)
                m |= 1u << WHITE[imin(lev[i].tick / 24, n - 1) / per];
        if (s != LOOP_IDLE)
            at = imin((int)loop_pos / 24, n - 1) / per;
    }
    if (at >= 0)
        m ^= 1u << WHITE[at];
    if (wr >= 0)                                            /* the step being written blinks */
        m = blink ? m | 1u << WHITE[wr] : m & ~(1u << WHITE[wr]);
    *out = m;
    return 1;
}

/* ---- the block ------------------------------------------------------------------------- */
/* An instrument's own volume as a gain: a square, as MASTER's is. */
static f32 inst_gain(int i)
{
    f32 v = (f32)INST_VOL(i) * 0.01f;
    return v * v;
}

static void switch_instrument(int i)
{
    int v, j;
    cur_i = i;
    cur = insts[i];
    for (v = 0; v < NVOICE; v++) {
        uint32_t serial = voices[v].serial;
        uint8_t *p = (uint8_t *)&voices[v];
        for (j = 0; j < (int)sizeof(Voice); j++)
            p[j] = 0;
        voices[v].serial = serial;
        voices[v].idx = (uint8_t)v;
        for (j = 0; j < KS_LEN; j++)
            ks_line[v][0][j] = ks_line[v][1][j] = 0.0f;
    }
    ir[i].at = blkno - 2u;                       /* (its knobs are taken as they stand, not eased from where they were) */
    for (j = 0; j < NINST; j++)
        ir[j].linger = 0;                        /* (every voice is gone: nothing of another instrument rings on) */
}

/* An instrument's numbers for this control block: the knobs of the algorithm it is on eased (~12 ms), its
 * LFO moved on and put on its target, its envelope rates. Once a block for each instrument that sounds.
 * (Every algorithm has its own four knobs. The eased ones are those of the algorithm in hand, the only
 * ones a hand can turn, and they ease across a change of algorithm too. A part that goes on sounding on
 * another algorithm takes its knobs as they stand: part_knobs.) */
static f32 blk_time;
static void inst_touch(int i)
{
    IRun *r = &ir[i];
    const InstParams *ip = &st.ip[i];
    const uint8_t *kn = KNOBS_NOW(i);
    Lfo *lf = i == cur_i ? &lfo_live : &r->lf;
    f32 dt = (f32)CTRL * ISR, brate = SR / (f32)CTRL, lfo, depth, f;
    int k, target;
    if (r->at == blkno)
        return;
    if (r->at != blkno - 1u)                     /* it was silent: the knobs where they stand */
        for (k = 0; k < 4; k++)
            r->knob_s[k] = (f32)kn[k] * 0.01f;
    r->at = blkno;

    f = 0.05f * exp2fx((f32)ip->lfo[1] * 0.09f);
    lf->ph += f * dt;
    if (lf->ph >= 1.0f) {
        lf->ph -= (f32)ifloor(lf->ph);
        lf->sh = rng_s(i == cur_i ? &eng_rng : &lfo_rng);
    }
    lf->drift += (lf->sh - lf->drift) * minf(1.0f, f * dt * 3.0f);
    switch (ip->lfo[0]) {
    case 1: lfo = 4.0f * absf(lf->ph - 0.5f) - 1.0f; break;
    case 2: lfo = 1.0f - 2.0f * lf->ph; break;
    case 3: lfo = lf->ph < 0.5f ? 1.0f : -1.0f; break;
    case 4: lfo = lf->sh; break;
    case 5: lfo = clampf(lf->drift * 1.6f, -1.0f, 1.0f); break;
    default: lfo = tsin(lf->ph); break;
    }
    depth = (f32)ip->lfo[2] * 0.01f;
    target = ip->lfo[3];
    if (i == cur_i)
        lfo_out = lfo * depth;

    for (k = 0; k < 4; k++)
        r->knob_s[k] += ((f32)kn[k] * 0.01f - r->knob_s[k]) * 0.06f;
    r->vol = 1.0f;
    r->panmod = 0.0f;
    r->kmod_k = -1;
    r->b.bend = ((f32)st.glo[2] - 50.0f) * 0.01f;
    if (target == 0)
        r->b.bend += lfo * depth * depth * 12.0f;
    else if (target <= 4) {
        r->kmod_k = target - 1;
        r->kmod = lfo * depth * 0.5f;
    }
    else if (target == 5)
        r->vol = 1.0f - depth * (0.5f + 0.5f * lfo);
    else
        r->panmod = lfo * depth;
    r->b.sr = SR;
    r->b.isr = ISR;
    r->b.lfo = lfo;
    r->b.time = blk_time;
    r->b.bpm = (f32)iclamp(st.glo[0], 40, 240);

    r->att = dt / (0.0015f * exp2fx((f32)ip->env[0] * 0.114f));
    r->kd = onepole_k(0.02f * exp2fx((f32)ip->env[1] * 0.08f), brate);
    r->sus = (f32)ip->env[2] * 0.01f;
    r->kr = onepole_k(0.015f * exp2fx((f32)ip->env[3] * 0.084f), brate);
}

/* The instrument's block() for a part, if what the instrument holds is another part's numbers or its
 * own clocks have not moved in this control block. They move once a block, whoever asks first. */
static const Blk *part_set(int part)
{
    int i = PART_INST(part), k;
    IRun *r = &ir[i];
    inst_touch(i);
    if (r->owner == part && r->adv == blkno)
        return &r->b;
    r->b.algo = PART_ALGO(part);
    /* the part's own four knobs: the eased ones on the algorithm the instrument is on, and as they
     * stand on any other */
    if (PART_ALGO(part) == (st.ip[i].algo < NALGO ? st.ip[i].algo : 0))
        for (k = 0; k < 4; k++)
            r->b.k[k] = r->knob_s[k];
    else
        for (k = 0; k < 4; k++)
            r->b.k[k] = (f32)KNOBS(i, PART_ALGO(part))[k] * 0.01f;
    if (r->kmod_k >= 0)
        r->b.k[r->kmod_k] = sat(r->b.k[r->kmod_k] + r->kmod);
    r->b.step = r->adv == blkno ? 0.0f : (f32)CTRL * ISR;
    r->adv = blkno;
    r->owner = (uint8_t)part;
    insts[i]->block(&r->b);
    return &r->b;
}


/* ---- a part's voices, on one core or two ------------------------------------------------------
 * A voice's control block (its envelope, its glide, the instrument's render) touches the voice and
 * its own buffer and nothing else, so the voices of a part can be made in any order, or half of them
 * on a second core at the same time. They are mixed afterwards, in voice order, so the sound is the
 * same to the bit whichever core made which. */
static f32 vt[NVOICE][CTRL];                     /* each voice's block before it is mixed */
static struct {                                  /* what a voice's block needs: set before any job is handed over */
    const InstDef *def;
    const Blk *b;
    f32 att, kd, sus, kr, kg, dt;
} vj;
static int (*c2_run)(void (*fn)(uint32_t), uint32_t arg);
static int (*c2_wait)(void);

void eng_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void))
{
    c2_run = run;
    c2_wait = wait;
}

static void voice_block(int i)
{
    Voice *v = &voices[i];
    f32 e0 = v->env;
    switch (v->stage) {
    case 1:
        v->env += vj.att;
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->stage = 2;
        }
        break;
    case 2:
    case 3:
        v->env += (vj.sus - v->env) * vj.kd;
        break;
    default:
        v->env -= v->env * vj.kr + 0.00002f;
        if (v->env <= 0.0003f)
            v->env = 0.0f;
        break;
    }
    v->env_prev = e0;
    v->pitch += (v->target - v->pitch) * vj.kg;
    vj.def->render(v, vj.b, vt[i], CTRL);
    v->age += vj.dt;
}

static void voices_job(uint32_t mask)            /* (this is what the second core is handed) */
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (mask >> i & 1u)
            voice_block(i);
}

static void block(f32 *L, f32 *R)
{
    static f32 bl[CTRL], br[CTRL];               /* one instrument's voices, before its post */
    const InstParams *ip;
    const Blk *b;
    f32 dt = (f32)CTRL * ISR, bpm, dticks, kg, peak = 0.0f;
    uint32_t imask = 0;
    int i, n, inst, lay = layers(), live;

    blkno++;
    if (lay != lay_was) {                        /* LAYERS switched: what the sequencer holds, it held the other way */
        lay_was = lay;
        seq_release_all();
    }
    if (pending_i >= 0) {                        /* an instrument change: fade, then swap */
        int any = 0;
        for (i = 0; i < NVOICE; i++)
            any |= voices[i].stage;
        if (!any || ++fade_blocks > 24) {
            switch_instrument(pending_i);
            pending_i = -1;
        }
    }
    ip = &st.ip[cur_i];
    live = live_part();
    if (!lay)                                    /* without LAYERS a sounding note follows the algorithm in hand */
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && PART_INST(voices[i].part) == cur_i)
                voices[i].part = (uint8_t)live;
    bpm = (f32)iclamp(st.glo[0], 40, 240);
    dticks = bpm * (96.0f / 60.0f) * dt;
    beat_out = fractf(beat_out + dticks * (1.0f / 96.0f));

    blk_time = run_time;
    run_time += dt;
    blk = *part_set(live);                       /* the instrument in hand: its LFO, its knobs, its block() */

    /* the panel's events and the sequencers, after the instrument has its numbers for this block:
     * a note that starts in an instrument's very first block finds them set */
    while (pending_i < 0 && ev_r != ev_w) {
        uint32_t e = evq[ev_r];
        ev_r = (ev_r + 1u) & 63u;
        handle(e);
    }
    ip = &st.ip[cur_i];                          /* (LAYERS: an event may have changed the instrument in hand) */
    live = live_part();
    if (pending_i < 0) {
        if (st.seq[0] == SEQ_STEP) step_tick(dticks);
        else loop_tick(dticks);
        free_tick(dticks);
        arp_tick(ip, dticks);
    }

    kg = st.keys[2] ? onepole_k(0.004f * exp2fx((f32)st.keys[2] * 0.08f), SR / (f32)CTRL) : 1.0f;

    for (n = 0; n < CTRL; n++)
        L[n] = R[n] = 0.0f;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage)
            imask |= 1u << PART_INST(voices[i].part);

    /* Instrument by instrument: each part's voices into the instrument's own pair of buffers, then the
     * instrument's post over the lot. An instrument holds one part's numbers at a time, so its block()
     * runs again before each part, and the part in hand goes last: its numbers are what the post and
     * the next note find. */
    for (inst = 0; inst < NINST; inst++) {
        IRun *r = &ir[inst];
        const InstDef *def = insts[inst];
        uint8_t parts[NVOICE + 1];
        f32 fade = 1.0f, kr;
        int np = 0, p, j, in_hand = inst == cur_i, has_live = 0;
        if (imask >> inst & 1u)
            r->linger = LINGER;
        else if (!in_hand) {
            if (!r->linger) {
                r->ig = inst_gain(inst);         /* (silent: its volume is where it stands when it next sounds) */
                continue;
            }
            fade = (f32)r->linger * (1.0f / (f32)LINGER);     /* its post rings out, then it is left alone */
            r->linger--;
        }
        for (i = 0; i < NVOICE; i++) {
            int pt = voices[i].part;
            if (!voices[i].stage || PART_INST(pt) != inst)
                continue;
            if (in_hand && pt == live) {
                has_live = 1;
                continue;
            }
            for (j = 0; j < np && parts[j] != pt; j++)
                ;
            if (j == np)
                parts[np++] = (uint8_t)pt;
        }
        if (has_live)
            parts[np++] = (uint8_t)live;
        for (n = 0; n < CTRL; n++)
            bl[n] = br[n] = 0.0f;
        b = &r->b;
        for (p = 0; p < np; p++) {
            uint32_t mine = 0, other = 0;
            int nv = 0;
            b = part_set(parts[p]);
            kr = pending_i >= 0 ? 0.3f : r->kr;
            for (i = 0; i < NVOICE; i++)         /* every other voice of the part to the second core, if there is one */
                if (voices[i].stage && voices[i].part == parts[p]) {
                    if (c2_run && (nv & 1))
                        other |= 1u << i;
                    else
                        mine |= 1u << i;
                    nv++;
                }
            vj.def = def;
            vj.b = b;
            vj.att = r->att;
            vj.kd = r->kd;
            vj.sus = r->sus;
            vj.kr = kr;
            vj.kg = kg;
            vj.dt = dt;
            if (other) {
                int on2 = c2_run(voices_job, other);
                voices_job(mine);
                if (!on2 || c2_wait() != 0)      /* it did not take them, or it dropped them: here, then */
                    voices_job(other);
            } else
                voices_job(mine);
            for (i = 0; i < NVOICE; i++) {
                Voice *v = &voices[i];
                const f32 *tmp = vt[i];
                f32 e0, e1, g, gl, gr, de, pan;
                if (!((mine | other) >> i & 1u))
                    continue;
                e0 = v->env_prev;
                e1 = v->env;
                g = def->gain * r->vol;
                pan = clampf(v->pan + r->panmod, -1.0f, 1.0f);
                gl = g * (1.0f - maxf(0.0f, pan) * 0.8f);
                gr = g * (1.0f + minf(0.0f, pan) * 0.8f);
                de = (e1 - e0) * (1.0f / (f32)CTRL);
                for (n = 0; n < CTRL; n++) {
                    f32 s = tmp[n] * e0;
                    e0 += de;
                    bl[n] += s * gl;
                    br[n] += s * gr;
                }
                if (v->stage == 4 && v->env <= 0.0f)
                    v->stage = 0;
            }
        }
        if (in_hand && !has_live)
            b = part_set(live);
        if (def->post)
            def->post(b, bl, br, CTRL);
        {
            /* Into the mix at the instrument's own volume, after its post: turned down it is the same
             * sound, quieter. The gain eases (about 10 ms) and ramps across the block, so a turn of the
             * knob does not zip. At full, and that is how every instrument comes, the samples go in
             * untouched. */
            f32 tg = inst_gain(inst), g0 = r->ig, g1 = g0 + (tg - g0) * 0.07f;
            if (absf(tg - g1) < 0.0004f)
                g1 = tg;
            r->ig = g1;
            if (fade < 1.0f || g0 != 1.0f || g1 != 1.0f) {
                f32 g = g0 * fade, dg = (g1 - g0) * fade * (1.0f / (f32)CTRL);
                for (n = 0; n < CTRL; n++) {
                    L[n] += bl[n] * g;
                    R[n] += br[n] * g;
                    g += dg;
                }
            } else
                for (n = 0; n < CTRL; n++) {
                    L[n] += bl[n];
                    R[n] += br[n];
                }
        }
    }

    {
        uint32_t w = scope_w;
        for (n = 0; n < CTRL; n++) {
            f32 m = (L[n] + R[n]) * 0.5f, am = absf(m);
            scope[w & (SCOPE_N - 1)] = m;
            w++;
            if (am > peak)
                peak = am;
        }
        scope_w = w;
    }
    {
        f32 lv = level_out;
        lv += (peak - lv) * (peak > lv ? 0.5f : 0.02f);
        level_out = lv;
    }

    fx_block(L, R, &st.ip[cur_i], bpm);

    if (click_env > 0.001f) {
        for (n = 0; n < CTRL; n++) {
            f32 c;                               /* wood, not a beep: the pitch drops as it lands, and it is gone in a twentieth of a second */
            click_lp += (noise_s(&click_rng) - click_lp) * 0.2f;
            c = (tsin(click_ph) + 0.22f * click_pe * tsin(click_ph2) + 0.5f * click_pe * click_lp) * click_env * click_amp;
            click_ph += click_f * (1.0f + 1.3f * click_pe) * ISR;
            if (click_ph >= 1.0f) click_ph -= 1.0f;
            click_ph2 += click_f * 2.76f * ISR;
            if (click_ph2 >= 1.0f) click_ph2 -= 1.0f;
            click_pe *= 0.9972f;
            click_env *= 0.99925f;
            L[n] += c;
            R[n] += c;
        }
    }
}

#ifdef H1_FUZZ
uint32_t h1_fuzz_nan;
#endif

void eng_render(int32_t *lr, uint32_t n)
{
    static f32 L[CTRL], R[CTRL];
    static uint32_t have, pos;                   /* frames of a block not yet handed out */
    while (n) {
        uint32_t take, i;
        if (!have) {
            block(L, R);
            have = CTRL;
            pos = 0;
        }
        take = have < n ? have : n;
        for (i = 0; i < take; i++) {
#ifdef H1_FUZZ
            if (L[pos + i] != L[pos + i] || R[pos + i] != R[pos + i])
                h1_fuzz_nan++;                   /* (tests/fuzz.c: a sound that stopped being numbers) */
#endif
            lr[0] = (int32_t)(softclip(L[pos + i]) * 7549747.0f);     /* 0.9 of 24-bit full scale */
            lr[1] = (int32_t)(softclip(R[pos + i]) * 7549747.0f);
            lr += 2;
        }
        pos += take;
        have -= take;
        n -= take;
    }
}

void eng_init(uint32_t sample_rate)
{
    int i;
    if (sample_rate) {                           /* 0: keep the rate (a state load) */
        SR = (f32)sample_rate;
        ISR = 1.0f / SR;
    }
    dsp_init();
    fx_init(SR);
    {
        uint8_t *p = (uint8_t *)ir;
        uint32_t k;
        for (k = 0; k < sizeof ir; k++)
            p[k] = 0;
    }
    for (i = 0; i < NINST; i++)
        ir[i].ig = inst_gain(i);
    lay_was = layers();
    switch_instrument(st.inst < NINST ? st.inst : 0);
    for (i = 0; i < NVOICE; i++) {               /* nothing sounds and nothing runs: as the power coming on leaves it */
        voices[i].serial = 0;
        voices[i].stage = 0;
        voices[i].gate = 0;
        voices[i].seq = 0;
    }
    loop_state = LOOP_EMPTY;
    nlev = 0;
    loop_pos = 0.0f;
    loop_held[0] = loop_held[1] = loop_held[2] = loop_held[3] = 0;
    latched[0] = latched[1] = latched[2] = latched[3] = 0;
    phys_down = nheld = arp_ncur = 0;
    arp_acc = 1e9f;
    step_playing = step_write = step_cursor = step_at = 0;
    step_nsnd = step_gesture = 0;
    step_acc = step_off = 0.0f;
    free_on = 0;
    click_at = -1;
    ev_r = ev_w;                                 /* (and whatever the panel had asked for is not done) */
    pending_i = -1;
    fade_blocks = 0;
}

/* ---- for the main loop ------------------------------------------------------------------ */
void eng_snapshot(Vis *vis)
{
    int i;
    for (i = 0; i < NVOICE; i++) {
        const Voice *v = &voices[i];
        vis->v[i].on = v->stage != 0;
        vis->v[i].gate = v->gate;
        vis->v[i].note = v->note;
        vis->v[i].serial = v->serial;
        vis->v[i].pitch = v->pitch;
        vis->v[i].env = v->env;
        vis->v[i].age = v->age;
    }
    for (i = 0; i < 4; i++)
        vis->k[i] = blk.k[i];
    vis->level = level_out;
    vis->lfo = lfo_out;
    vis->beat = beat_out;
    vis->notes = note_count;
    vis->last_pitch = last_pitch;
    if (layers())                                /* the picture is the instrument in hand: its own notes move it */
        for (i = 0; i < NVOICE; i++)
            if (PART_INST(voices[i].part) != cur_i)
                vis->v[i].on = 0;
    vis->scope = scope;
    vis->scope_pos = (int)(scope_w & (SCOPE_N - 1));
}

uint32_t eng_sounding_lo(void)
{
    uint32_t m = 0;
    int i, base = 53 + 12 * st.octave;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate) {
            int k = (int)voices[i].note - base;
            if (k >= 0 && k < H1_NKEYS)
                m |= 1u << k;
        }
    return m;
}

/* main loop: is anything recorded that was played on this instrument (LAYERS: what holding REC would clear) */
int eng_seq_has(int inst)
{
    int i, j, ne = nlev;
    if (st.seq[0] == SEQ_STEP) {
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++)
                if (st.steps[i][j] && tag_inst(st.step_part[i][j]) == inst)
                    return 1;
        return 0;
    }
    for (i = 0; i < ne && i < NLEV; i++)
        if (tag_inst(lev[i].part) == inst)
            return 1;
    return 0;
}

uint32_t eng_voices(void)
{
    uint32_t n = 0;
    int i;
    for (i = 0; i < NVOICE; i++)
        n += voices[i].stage != 0;
    return n;
}
