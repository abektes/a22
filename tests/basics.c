/* What every a22 engine and the panel around them owe a player, on the core alone, its panel worked as a
 * hand would. Run by tests/run_tests.sh. Carried over from h1's tests/v022.c, without what a22 does not
 * have (layers, h1's saved states):
 *
 *   the limiter     a chord on every algorithm of every engine never goes past the limiter's ceiling,
 *                   and is heard
 *   velocity        a soft note over MIDI is quieter and darker than a hard one; 127 over MIDI is the
 *                   panel's key to the bit
 *   the tape saved  h1_loop / h1_loop_load bring a recording back after a power cycle, from any address;
 *                   it asks to be saved when it changed and not once it is
 *   WAVE's SPREAD   at 0 a note is centred (both sides the same), and it grows wider over the knob's
 *                   whole turn, on every bank of WAVE and of CYCLE
 *   CYCLE's STEP-   its STEP- algorithms read whole waves (nearby K1 sounds the same), the others morph; with
 *                   SPREAD up, the right side glides from wave to wave as smoothly as the left
 *   MIDI out        keys, the arpeggiator and the tape go out, CC 20-23 with the knobs, nothing that came in
 *                   is sent back, and HUSH tells the computer (CC 123)
 *
 * Each sound to compare is made in a process of its own: h1_init does not wind the engine's clocks and
 * random numbers back, so two takes in one process never sound the same. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct Heard { double energy, slope; int32_t peak; uint32_t hash, n; } Heard;
static Heard heard;
static int32_t last_l;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j], a = s < 0 ? -s : s;
            double d = (double)s - (double)last_l;
            heard.energy += (double)s * (double)s;
            heard.slope += d * d;                /* the energy of the first difference: how bright */
            if (a > heard.peak) heard.peak = a;
            heard.hash = heard.hash * 31u + (uint32_t)s;
            last_l = s;
            heard.n++;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void listen(void) { memset(&heard, 0, sizeof heard); }
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }

/* a take in a process of its own */
static Heard taken(Heard (*fn)(int, int), int a, int b)
{
    int fd[2];
    Heard h;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        h = fn(a, b);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h) {
        printf("FAIL: a take did not come back\n");
        exit(1);
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

/* ---- the limiter: six keys at once, on engine i, algorithm a, the four knobs at KV[ka >> 3] ---- */
static const int KV[3] = {-1, 0, 100};           /* (-1: as they come) */
static Heard chord(int i, int ka)
{
    static const int K[6] = {0, 7, 12, 16, 19, 24};
    int k, a = ka & 7, kv = KV[(ka >> 3) % 3];
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, i);
    h1_control_set(H1_ENC_ALGO, a);
    if (kv >= 0 && kv <= 100) {
        h1_control_set(H1_ENC_K1, kv);
        h1_control_set(H1_ENC_K2, kv);
        h1_control_set(H1_ENC_K3, kv);
        h1_control_set(H1_ENC_K4, kv);
    }
    go(300);
    listen();
    for (k = 0; k < 6; k++)
        h1_key(K[k], 1);
    go(1500);
    for (k = 0; k < 6; k++)
        h1_key(K[k], 0);
    go(1000);
    return heard;
}

/* ---- velocity: one note on FM-4 KEYS, from the panel (vel 0) or over MIDI at vel ---- */
static Heard one_note(int vel, int unused)
{
    (void)unused;
    h1_init(44100);
    go(300);
    listen();
    if (vel)
        h1_midi(0x90, 60, (uint8_t)vel);
    else
        h1_key(7, 1);                            /* (key 7 is note 60) */
    go(700);
    if (vel)
        h1_midi(0x80, 60, 0);
    else
        h1_key(7, 0);
    go(800);
    return heard;
}

/* ---- WAVE's SPREAD: a held C4 on algorithm a (0-7 WAVE, 8-15 CYCLE: the same player), SPREAD at sp, no FX;
 * .energy is the side's over the middle's ---- */
static Heard wave_width(int a, int sp)
{
    static int32_t out[512];
    double mid = 0.0, side = 0.0;
    int i, j;
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, a < 8 ? 10 : 12);   /* (WAVE, CYCLE) */
    h1_control_set(H1_ENC_ALGO, a & 7);
    h1_control_set(H1_ENC_K3, sp);
    st.fx_on = 0;
    go(300);
    h1_key(7, 1);
    go(250);                                     /* (past the attack) */
    for (i = 0; i < 260; i++) {                  /* (1.5 s) */
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
        for (j = 0; j < 512; j += 2) {
            double m = 0.5 * ((double)out[j] + (double)out[j + 1]), d = 0.5 * ((double)out[j] - (double)out[j + 1]);
            mid += m * m;
            side += d * d;
        }
    }
    memset(&heard, 0, sizeof heard);
    heard.energy = mid > 0.0 ? side / mid : -1.0;
    return heard;
}

/* ---- CYCLE's STEP- algorithms: a held note on algorithm a with K1 at k1 (SCAN at 0), heard as a hash ---- */
static Heard cycle_tone(int a, int k1)
{
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, 12);           /* (CYCLE) */
    h1_control_set(H1_ENC_ALGO, a);
    h1_control_set(H1_ENC_K1, k1);
    h1_control_set(H1_ENC_K2, 0);
    st.fx_on = 0;
    go(400);
    listen();
    h1_key(7, 1);
    go(600);
    h1_key(7, 0);
    go(200);
    return heard;
}

/* ---- CYCLE's STEP- algorithms with SPREAD up: a note held while SCAN moves it through the bank, each side's
 * largest kink (second difference) over its rms; heard.energy is the right side's over the left's ---- */
static Heard cycle_sides(int a, int unused)
{
    static int32_t out[512];
    double kink[2] = {0.0, 0.0}, e[2] = {0.0, 0.0}, p1[2] = {0.0, 0.0}, p2[2] = {0.0, 0.0};
    int i, j, c, n = 0;
    (void)unused;
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, 12);           /* (CYCLE) */
    h1_control_set(H1_ENC_ALGO, a);
    h1_control_set(H1_ENC_K1, 0);
    h1_control_set(H1_ENC_K2, 100);
    h1_control_set(H1_ENC_K3, 100);
    h1_control_set(H1_ENC_K4, 100);
    st.fx_on = 0;
    go(300);
    h1_key(7, 1);
    for (i = 0; i < 2500 * 441 / 2560; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
        for (j = 0; j < 512; j += 2, n++)
            for (c = 0; c < 2; c++) {
                double x = out[j + c], k = fabs(x - 2.0 * p1[c] + p2[c]);
                if (n > 2000) {                  /* (past the note's start) */
                    if (k > kink[c]) kink[c] = k;
                    e[c] += x * x;
                }
                p2[c] = p1[c];
                p1[c] = x;
            }
    }
    h1_key(7, 0);
    memset(&heard, 0, sizeof heard);
    heard.energy = e[0] > 0.0 && e[1] > 0.0 && kink[0] > 0.0 ? (kink[1] / sqrt(e[1])) / (kink[0] / sqrt(e[0])) : -1.0;
    return heard;
}

/* ---- MIDI out ---- */
static uint8_t mo[1024][3];
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (nmo < 1024) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        nmo++;
    }
}
static int mo_count(uint8_t s, int note)          /* (note < 0: any) */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == s && (note < 0 || mo[i][1] == note))
            n++;
    return n;
}

static int seq_voices(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate && voices[v].seq;
    return n;
}

static int loop_state(void)
{
    SeqInfo si;
    eng_seq_info(&si);
    return si.loop_state;
}

int main(void)
{
    static uint8_t blob[sizeof(H1State)], lblob[H1_LOOP_MAX];
    const uint8_t *p;
    uint32_t size, lsize;
    int i, a, most;

    /* ---- the limiter: every algorithm with its knobs as they come, then all at the bottom and the top ---- */
    {
        /* a22's limiter (core/synth.c): the gain holds a block under LIM_T 0.78, and limit() bends that to 0.770
         * of the converter's scale (7549747) */
        int32_t ceiling = (int32_t)(0.7705f * 7549747.0f), worst = 0, quietest = 0x7FFFFFFF;
        int kv;
        for (i = 0; i < NINST; i++)
            for (a = 0; a < NALGOS(i); a++)
                for (kv = 0; kv < 3; kv++) {
                    Heard h = taken(chord, i, a | kv << 3);
                    if (h.peak > worst) worst = h.peak;
                    if (h.peak < quietest) quietest = h.peak;
                    if (h.peak > ceiling)
                        printf("  %s %s knobs %d: peak %d over %d\n", insts[i]->name, insts[i]->algo[a], KV[kv],
                               (int)h.peak, (int)ceiling);
                    CHECK(h.peak <= ceiling, "six keys never pass the limiter's ceiling");
                    if (KV[kv] < 0) {
                        if (h.peak <= 200000)
                            printf("  %s %s: peak only %d\n", insts[i]->name, insts[i]->algo[a], (int)h.peak);
                        CHECK(h.peak > 200000, "and with the knobs as they come, they are heard");
                    }
                }
        printf("  limiter: the loudest chord peaks at %.3f of full scale, the quietest at %.3f\n",
               (double)worst / 8388607.0, (double)quietest / 8388607.0);
    }

    /* ---- velocity ---- */
    {
        Heard key = taken(one_note, 0, 0), hard = taken(one_note, 127, 0), soft = taken(one_note, 40, 0);
        double bright_hard = hard.slope / hard.energy, bright_soft = soft.slope / soft.energy;
        CHECK(key.hash == hard.hash && key.energy == hard.energy, "127 over MIDI is the panel's key, to the bit");
        CHECK(soft.energy < hard.energy * 0.25, "velocity 40 is much quieter");
        CHECK(soft.energy > 0.0, "and still heard");
        CHECK(bright_soft < bright_hard * 0.9, "and darker");
        printf("  velocity 40: %.1f dB, brightness %.2f of 127's\n", 10.0 * log10(soft.energy / hard.energy),
               bright_soft / bright_hard);
    }

    /* ---- WAVE's SPREAD: centred at 0, then wider at every quarter of the turn ---- */
    {
        static const int SP[5] = {0, 25, 50, 75, 100};
        for (a = 0; a < 16; a++) {
            const InstDef *wd = a < 8 ? &inst_wave : &inst_cycle;
            double w[5];
            int k, wider = 1;
            for (k = 0; k < 5; k++)
                w[k] = taken(wave_width, a, SP[k]).energy;
            for (k = 1; k < 5; k++)
                wider &= w[k] > w[k - 1] * 1.5;
            if (w[0] >= 0.02 || !wider || w[4] < 0.3)
                printf("  %s %s: side/middle %.4f %.4f %.4f %.4f %.4f at SPREAD 0, 25, 50, 75, 100\n",
                       wd->name, wd->algo[a & 7], w[0], w[1], w[2], w[3], w[4]);
            CHECK(w[0] >= 0.0 && w[0] < 0.02, "WAVE and CYCLE at SPREAD 0 are centred");
            CHECK(wider, "WAVE and CYCLE grow wider at every quarter of SPREAD's turn");
            CHECK(w[4] >= 0.3, "WAVE and CYCLE at full SPREAD are wide");
        }
    }

    /* ---- CYCLE: the STEP- algorithms read whole waves (K1 at 29 and 31 are both place 4), the others morph ---- */
    for (a = 0; a < 4; a++) {
        Heard s29 = taken(cycle_tone, a + 4, 29), s31 = taken(cycle_tone, a + 4, 31);
        Heard m29 = taken(cycle_tone, a, 29), m31 = taken(cycle_tone, a, 31);
        CHECK(s29.hash == s31.hash && s29.energy > 0.0, "a STEP- algorithm plays the same whole wave for nearby K1");
        CHECK(m29.hash != m31.hash, "a smooth algorithm morphs between them");
    }
    /* and with SPREAD up the right side glides from wave to wave as the left does: no sharper kinks */
    for (a = 4; a < 8; a++) {
        Heard s = taken(cycle_sides, a, 0);
        if (s.energy < 0.0 || s.energy > 1.5)
            printf("  CYCLE %s, SPREAD 100: the right side's largest kink %.2f times the left's\n",
                   h1_algo_name(12, a), s.energy);
        CHECK(s.energy > 0.0 && s.energy <= 1.5, "a STEP- algorithm's right side steps as smoothly as its left");
    }

    /* ---- the tape: a take, its notes and a knob out over MIDI, saved, a power cycle, loaded, played ---- */
    h1_init(44100);
    nmo = 0;
    h1_midi_out(mo_fn);
    go(100);
    tap(H1_BTN_REC);                             /* armed: the first key starts the take */
    CHECK(loop_state() == LOOP_ARMED, "REC arms the tape");
    for (i = 0; i < 6; i++) {
        note(i * 2, 150);
        if (i == 2)
            h1_encoder(H1_ENC_K1, 5);
    }
    CHECK(loop_state() == LOOP_REC, "the first key starts the take");
    tap(H1_BTN_REC);                             /* the take ends: the tape plays it */
    CHECK(loop_state() == LOOP_PLAY, "REC again ends it, and the tape plays");
    CHECK(mo_count(0xB0, 20) > 0, "the knob went out as CC 20");
    nmo = 0;
    go(9000);                                    /* (a pass of the 4-bar tape: the take lies at its start) */
    CHECK(mo_count(0x90, -1) > 0 && mo_count(0x80, -1) > 0, "the tape's notes go out over MIDI");
    CHECK(h1_loop_dirty(), "a recording that changed asks to be saved");
    tap(H1_BTN_PLAY);                            /* stop */
    go(200);
    CHECK(h1_loop_dirty(), "and still, stopped");
    p = (const uint8_t *)h1_loop(&lsize);
    CHECK(lsize > 12u && lsize <= H1_LOOP_MAX, "the tape as a blob");
    memcpy(lblob, p, lsize);
    h1_loop_saved();
    CHECK(!h1_loop_dirty(), "and is clean once saved");
    p = (const uint8_t *)h1_state(&size);
    memcpy(blob, p, size);
    h1_init(44100);                              /* a power cycle */
    go(100);
    CHECK(h1_state_load(blob, size) == 0, "the state loads");
    CHECK(h1_loop_load(lblob + 1, lsize - 1) != 0, "a broken tape is refused");
    CHECK(h1_loop_load(lblob, lsize) == 0, "the tape loads");
    CHECK(!h1_loop_dirty(), "a loaded tape is not saved again");
    CHECK(loop_state() == LOOP_IDLE, "stopped, ready to play");
    {
        uint32_t again;
        p = (const uint8_t *)h1_loop(&again);
        CHECK(again == lsize && memcmp(p, lblob, lsize) == 0, "saved again, the tape is the same to the byte");
        p = (const uint8_t *)h1_state(&again);
        CHECK(again == size && memcmp(p, blob, size) == 0, "and so is the state");
    }
    tap(H1_BTN_PLAY);
    most = 0;
    for (i = 0; i < 400; i++) {
        go(12);
        if (seq_voices() > most) most = seq_voices();
    }
    CHECK(most > 0, "and it plays after the power cycle");
    tap(H1_BTN_PLAY);
    {
        static uint8_t odd[H1_LOOP_MAX + 3];
        memcpy(odd + 1, lblob, lsize);
        CHECK(h1_loop_load(odd + 1, lsize) == 0, "from any address");
    }

    /* ---- MIDI out: keys, MIDI in, the arpeggiator, HUSH ---- */
    h1_init(44100);
    nmo = 0;
    h1_midi_out(mo_fn);
    go(100);
    note(7, 100);
    CHECK(mo_count(0x90, 60) == 1 && mo_count(0x80, 60) == 1, "a key goes out, on and off");
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == 0x90 && mo[i][1] == 60)
            CHECK(mo[i][2] == 127, "at velocity 127");
    nmo = 0;
    h1_midi(0x90, 64, 90);
    go(100);
    h1_midi(0x80, 64, 0);
    go(100);
    CHECK(mo_count(0x90, -1) == 0 && mo_count(0x80, -1) == 0, "what came in over MIDI is not sent back");
    st.ip[st.inst].arp[0] = ARP_UP;
    nmo = 0;
    h1_midi(0x90, 64, 90);                       /* the arpeggiator plays what is held, wherever it came from */
    h1_midi(0x90, 67, 90);
    go(1500);
    h1_midi(0x80, 64, 0);
    h1_midi(0x80, 67, 0);
    go(500);
    CHECK(mo_count(0x90, 64) > 0 && mo_count(0x90, 67) > 0, "the arpeggiator's notes go out");
    CHECK(mo_count(0x90, -1) == mo_count(0x80, -1), "each with its note-off");
    st.ip[st.inst].arp[0] = ARP_OFF;
    nmo = 0;
    h1_midi(0xB0, 123, 0);                       /* all notes off from the computer: not sent back (a computer that */
    go(100);                                     /* passes MIDI through would return it, for ever) */
    CHECK(mo_count(0xB0, 123) == 0, "a CC 123 that came in is not sent back");
    nmo = 0;
    h1_button(H1_BTN_HOME, 1);                   /* HOME held: HUSH, and the computer is told */
    go(1000);
    h1_button(H1_BTN_HOME, 0);
    go(100);
    CHECK(mo_count(0xB0, 123) == 1, "HUSH sends CC 123");
    h1_midi_out(0);

    /* What the engine writes is saved as a setting is: a riff, a step, a knob set over MIDI. (Only the panel marked
     * the state, so these reached flash only if some later setting changed.) */
    {
        int i, j, stepped = 0;
        h1_init(44100);
        go(2500); h1_state_saved(); go(2000);
        CHECK(!h1_state_dirty(), "saving: nothing changed, nothing to save");
        tap(H1_BTN_ARP); h1_encoder(H1_ENC_ALGO, 1);             /* RIFF */
        go(2000); h1_state_saved(); go(100);
        tap(H1_BTN_REC); note(7, 100); note(9, 100); tap(H1_BTN_REC);
        go(2000);
        CHECK(st.riff_len == 2 && h1_state_dirty(), "saving: a riff recorded is saved");
        h1_encoder(H1_ENC_ALGO, -1);                                /* OFF */
        tap(H1_BTN_HOME);
        h1_button(H1_BTN_SEQ, 1); go(700); h1_button(H1_BTN_SEQ, 0); /* the step sequencer */
        go(2000); h1_state_saved(); go(100);
        tap(H1_BTN_REC); note(7, 100); tap(H1_BTN_REC);
        go(2000);
        for (i = 0; i < (int)(sizeof st.steps / sizeof st.steps[0]); i++)
            for (j = 0; j < (int)sizeof st.steps[0]; j++)
                stepped += st.steps[i][j] != 0;
        CHECK(stepped == 1 && h1_state_dirty(), "saving: a step written is saved");
        h1_state_saved(); go(100);
        h1_midi(0xB0, 20, 99);
        go(2000);
        CHECK(h1_state_dirty(), "saving: a knob set over MIDI CC 20 is saved");
    }

    /* The review's small ones: the event queue keeps room for note-offs; a snapshot's LFO is clamped on load; the TAPE
     * EDIT page counts bars from 1 and shows END for an OUT that plays to the end; holding SAVE resets the track's
     * algorithm too */
    {
        static uint8_t buf[sizeof(H1State)];
        uint32_t size, d0;
        int i, lo, hi;
        const char *tx = 0;
        h1_init(44100);
        go(200);
        eng_post_note(1, 41, 100, 1);            /* a note on, played */
        go(100);
        d0 = eng_events_dropped();
        for (i = 0; i < 80; i++) eng_post(EV_AUTOMATION, 0);   /* more events than the queue holds */
        eng_post_note(0, 41, 0, 1);              /* and the note's off after them */
        CHECK(eng_events_dropped() > d0, "queue: what does not fit is counted as dropped");
        go(200);
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && voices[i].gate && voices[i].note == 41) break;
        CHECK(i == NVOICE, "queue: the note-off after the flood still got in (its note ends)");
        eng_post(EV_PANIC, 1);
        go(200);

        memcpy(buf, h1_state(&size), size);
        ((H1State *)buf)->snap[0].used = 1;
        ((H1State *)buf)->snap[0].lfo[0] = 200;
        ((H1State *)buf)->snap[0].lfo[1] = 250;
        ((H1State *)buf)->snap[0].lfo[3] = 99;
        h1_state_load(buf, size);
        CHECK(st.snap[0].lfo[0] <= 6 && st.snap[0].lfo[1] <= 100 && st.snap[0].lfo[3] <= 6, "load: a snapshot's LFO comes back in range");

        h1_init(44100);
        go(200);
        st.seq[0] = SEQ_TAPE; st.seq[1] = 2;     /* 4 bars */
        tap(H1_BTN_SEQ); h1_encoder(H1_ENC_SELECT, 1);   /* TAPE EDIT */
        st.tape[1] = 0; st.tape[2] = 0;
        h1_control(H1_ENC_K1, &lo, &hi, 0, &tx);
        CHECK(tx && !strcmp(tx, "1"), "TAPE EDIT: IN BAR counts from 1");
        st.tape[1] = 2; st.tape[2] = 1;
        h1_control(H1_ENC_K2, &lo, &hi, 0, &tx);
        CHECK(tx && !strcmp(tx, "END"), "TAPE EDIT: an OUT BAR before IN BAR shows END, as it plays");
        st.tape[2] = 4;
        h1_control(H1_ENC_K2, &lo, &hi, 0, &tx);
        CHECK(tx && !strcmp(tx, "4"), "TAPE EDIT: an OUT BAR after IN BAR shows its bar");
        tap(H1_BTN_HOME);

        st.ip[st.inst].algo = (uint8_t)((insts[st.inst]->def.algo + 3) & 7);
        st.trk[st.track].algo = st.ip[st.inst].algo;
        h1_button(H1_BTN_SAVE, 1); go(2500); h1_button(H1_BTN_SAVE, 0); go(100);   /* the engine reset */
        CHECK(st.ip[st.inst].algo == insts[st.inst]->def.algo && st.trk[st.track].algo == (st.ip[st.inst].algo & 7),
              "SAVE held: the engine reset, and the track plays its reset algorithm");
    }

    printf("basics: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
