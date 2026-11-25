/* KEY FOLLOW's data, on the core alone. Run by tests/run_tests.sh.
 *
 *   recorded notes   a take's notes carry GLO's key (ROOT and SCALE as they were), an off its on's, and the blob's
 *                    header says so
 *   blob             saved, loaded and saved again: the same bytes; keys the header does not vouch for, or that
 *                    are not keys, load as none
 *   state            the step pattern's key and each track's KEY bit: kept when valid, none when not
 */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t ms;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void track(int d)                         /* EDIT held, PRESETS turned, EDIT let go */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_encoder(H1_ENC_PRESET, d);
    h1_button(H1_BTN_EDIT, 0);
    go(40);
}

/* ---- what went out ---- */
static uint8_t mo[4096][3];
static int nmo;
static int rt_n[8], rt_seq[64], rt_at[64], nrt; /* real time: counts by status - 0xF8, and Start / Stop in order,
                                                  * with how many other messages had gone out before each */
static void rt_reset(void) { memset(rt_n, 0, sizeof rt_n); nrt = 0; }
static int f8_at_start;                          /* the clock count when the last Start went out */
static int stamp[64], nstamp, stamping;         /* the clock count at each note-on of note 60, while stamping */
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (stamping && s == 0x90 && a == 60 && b && nstamp < 64)
        stamp[nstamp++] = rt_n[0];
    if (s >= 0xF8) {
        rt_n[s - 0xF8]++;
        if (s == 0xFA)
            f8_at_start = rt_n[0];
        if (s != 0xF8 && nrt < 64) {
            rt_at[nrt] = nmo;
            rt_seq[nrt++] = s;
        }
        return;
    }
    if (nmo < 4096) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        nmo++;
    }
}
static int mo_n(uint8_t s, int d1)               /* (d1 < 0: any) */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == s && (d1 < 0 || mo[i][1] == d1))
            n++;
    return n;
}
static int first_on(int from)                    /* where the first note-on at or after FROM is in mo[], or -1 */
{
    int i;
    for (i = from; i < nmo; i++)
        if ((mo[i][0] & 0xF0) == 0x90 && mo[i][2])
            return i;
    return -1;
}
static int exact(void)                           /* every channel's every note: exactly as many offs as ons (no
                                                  * stray off on a channel, which would cut another note there) */
{
    static int open[16][128];
    int i, c, k;
    memset(open, 0, sizeof open);
    for (i = 0; i < nmo; i++) {
        int t = mo[i][0] & 0xF0, ch = mo[i][0] & 15, n = mo[i][1] & 127;
        if (t == 0x90 && mo[i][2]) open[ch][n]++;
        else if (t == 0x80 || t == 0x90) open[ch][n]--;
    }
    for (c = 0; c < 16; c++)
        for (k = 0; k < 128; k++)
            if (open[c][k]) {
                printf("  channel %d note %d: %+d ons over offs\n", c + 1, k, open[c][k]);
                return 0;
            }
    return 1;
}
static int balanced(void)                        /* every channel's every note: no on left without its off (a
                                                  * spare off is harmless, and the arpeggiator sends one today) */
{
    static int open[16][128];
    int i, c, k;
    memset(open, 0, sizeof open);
    for (i = 0; i < nmo; i++) {
        int t = mo[i][0] & 0xF0, ch = mo[i][0] & 15, n = mo[i][1] & 127;
        if (t == 0x90 && mo[i][2]) open[ch][n]++;
        else if (t == 0x80 || t == 0x90) open[ch][n]--;
    }
    for (c = 0; c < 16; c++)
        for (k = 0; k < 128; k++)
            if (open[c][k] > 0) {
                printf("  channel %d note %d: %d on without its off\n", c + 1, k, open[c][k]);
                return 0;
            }
    return 1;
}
static int loop_state(void)
{
    SeqInfo si;
    eng_seq_info(&si);
    return si.loop_state;
}
static void fresh(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        h1_key(k, 0);
    h1_init(44100);
    ms = 0;
    nmo = 0;
    rt_reset();
    h1_midi_out(mo_fn);
    go(100);
}


#define LEV_HEAD 12
static int is_key(uint8_t k, int root, int scale) { return k == key_byte(root, scale); }

static void recorded_notes_carry_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, seen = 0, ok = 1;
    fresh();
    st.keys[0] = 2;                              /* ROOT D */
    st.keys[1] = 2;                              /* SCALE MINOR */
    tap(H1_BTN_REC);
    note(7, 150);
    note(9, 150);
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    CHECK(p && size >= LEV_HEAD + 32u, "the take is in the blob");
    CHECK(p[5] & 1, "the header says the events carry keys");
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        if ((e[3] & 7) == 1 || (e[3] & 7) == 0) {
            seen++;
            if (!is_key(e[7], 2, 2)) ok = 0;
        }
    }
    CHECK(seen == 4, "two ons and two offs");
    CHECK(ok, "each note carries D minor");
}

static void off_takes_its_ons_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, ok = 1, seen = 0;
    fresh();
    st.keys[0] = 2;
    st.keys[1] = 2;
    tap(H1_BTN_REC);
    h1_key(7, 1);
    go(150);
    st.keys[0] = 9;                              /* ROOT turned while the key is held */
    st.keys[1] = 5;
    h1_key(7, 0);
    go(60);
    note(9, 150);                                /* this one is in the new key */
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        int n = e[2];
        if ((e[3] & 7) > 1) continue;
        seen++;
        if (!is_key(e[7], n == 60 ? 2 : 9, n == 60 ? 2 : 5)) ok = 0;
    }
    CHECK(seen == 4 && ok, "an off has its on's key, though ROOT moved in between");
}

static void arp_notes_carry_key(void)
{
    const uint8_t *p;
    uint32_t size, i, ne, seen = 0, ok = 1;
    fresh();
    st.keys[0] = 5;
    st.keys[1] = 3;
    st.ip[st.inst].arp[0] = ARP_UP;
    tap(H1_BTN_REC);
    h1_key(7, 1);
    h1_key(11, 1);
    go(800);
    h1_key(7, 0);
    h1_key(11, 0);
    go(200);
    tap(H1_BTN_REC);
    p = (const uint8_t *)h1_loop(&size);
    ne = p[6] | p[7] << 8;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + LEV_HEAD + i * 8u;
        if ((e[3] & 7) > 1) continue;
        seen++;
        if (!is_key(e[7], 5, 3)) ok = 0;
    }
    CHECK(seen > 2 && ok, "the arpeggiator's notes carry the key too");
}

static uint8_t blob[LEV_HEAD + 8 * 4];
static void mkblob(uint8_t flag, const uint8_t *keys)
{
    int i;
    memset(blob, 0, sizeof blob);
    blob[0] = 'A'; blob[1] = '2'; blob[2] = '2'; blob[3] = 'T';
    blob[4] = 1; blob[5] = flag;
    blob[6] = 4;
    blob[8] = 0x00; blob[9] = 0x06;              /* 1536 ticks */
    for (i = 0; i < 4; i++) {
        uint8_t *e = blob + LEV_HEAD + i * 8;
        e[0] = (uint8_t)(i * 20);
        e[2] = 60;
        e[3] = (uint8_t)(i & 1 ? 0 : 1);
        e[5] = 0;
        e[6] = 90;
        e[7] = keys[i];
    }
}
static void blob_round_trip(void)
{
    const uint8_t *p, *q;
    static uint8_t a[4096];
    uint32_t n, m;
    const uint8_t k[4] = {0x25, key_byte(4, 1), key_byte(0, 7), 0x8C};
    fresh();
    mkblob(1, k);
    CHECK(h1_loop_load(blob, sizeof blob) == 0, "a blob with keys loads");
    p = (const uint8_t *)h1_loop(&n);
    memcpy(a, p, n);
    CHECK(a[5] & 1, "it saves with the header bit");
    CHECK(a[LEV_HEAD + 7] == 0 && a[LEV_HEAD + 15] == key_byte(4, 1) && a[LEV_HEAD + 23] == key_byte(0, 7) &&
          a[LEV_HEAD + 31] == 0, "valid keys kept; no valid bit, or root 12, dropped");
    fresh();
    CHECK(h1_loop_load(a, n) == 0, "and loads again");
    q = (const uint8_t *)h1_loop(&m);
    CHECK(m == n && !memcmp(q, a, n), "saved, loaded and saved again: the same bytes");
}
static void junk_keys_dropped(void)
{
    const uint8_t *p;
    uint32_t n, i, ok = 1;
    const uint8_t k[4] = {0xA5, key_byte(4, 1), 0xFF, 0x91};
    fresh();
    mkblob(0, k);
    CHECK(h1_loop_load(blob, sizeof blob) == 0, "a blob without the header bit loads");
    p = (const uint8_t *)h1_loop(&n);
    for (i = 0; i < 4; i++)
        if (p[LEV_HEAD + i * 8 + 7]) ok = 0;
    CHECK(ok, "with every key 0, whatever its events held");
}

static void state_rsv_valid(void)
{
    static uint8_t a[4096];
    const void *p;
    uint32_t n;
    fresh();
    st.rsv[0] = key_byte(4, 1);
    st.trk[2].rsv = 1;
    p = h1_state(&n);
    memcpy(a, p, n);
    fresh();
    CHECK(h1_state_load(a, n) == 0, "the state loads");
    CHECK(st.rsv[0] == key_byte(4, 1) && st.trk[2].rsv == 1 && st.rsv[1] == 0, "a valid key and a KEY bit are kept");
    ((H1State *)a)->rsv[0] = 0x8C;
    ((H1State *)a)->rsv[1] = 0x55;
    ((H1State *)a)->trk[2].rsv = 0xFE;
    ((H1State *)a)->trk[1].rsv = 0xFF;
    CHECK(h1_state_load(a, n) == 0, "a state with junk there loads");
    CHECK(st.rsv[0] == 0 && st.rsv[1] == 0 && st.trk[2].rsv == 0 && st.trk[1].rsv == 1,
          "root 12 is no key; the KEY bit is bit 0 alone");
    ((H1State *)a)->rsv[0] = 0x25;
    h1_state_load(a, n);
    CHECK(st.rsv[0] == 0, "no valid bit, no key");
    fresh();
    CHECK(st.rsv[0] == 0 && st.rsv[1] == 0 && st.trk[0].rsv == 0 && st.trk[3].rsv == 0, "defaults: none");
}

int main(void)
{
    recorded_notes_carry_key();
    off_takes_its_ons_key();
    arp_notes_carry_key();
    blob_round_trip();
    junk_keys_dropped();
    state_rsv_valid();
    h1_midi_out(0);
    printf("keyfollow: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
