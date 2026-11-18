/* h1 - playing the recordings: see smp.h. */
#include "smp.h"

int smp_pick(int bank, f32 pitch)
{
    const SmpBank *b = &smp_banks[bank >= 0 && bank < SMP_NBANK ? bank : 0];
    const SmpZone *z = &smp_zones[b->first];
    int i;
    for (i = 1; i < b->n; i++)                   /* (low to high: stop at the first that is further off than the last) */
        if (absf(z[i].key - pitch) >= absf(z[i - 1].key - pitch))
            break;
    return b->first + i - 1;
}

const SmpZone *smp_zone(int bank, uint32_t zone, f32 pitch)
{
    const SmpBank *b = &smp_banks[bank >= 0 && bank < SMP_NBANK ? bank : 0];
    if (zone < b->first || zone >= (uint32_t)b->first + b->n)
        zone = (uint32_t)smp_pick(bank, pitch);
    return &smp_zones[zone];
}

void smp_fit(const SmpZone *z, f32 pitch, f32 *g, f32 *t60)
{
    /* (a string an octave up goes round twice as often: half the loss a turn keeps its harmonics dying as fast
     * as they did, and a little faster) */
    f32 up = clampf(pitch - z->key - 6.0f, 0.0f, 48.0f);
    *g = 1.0f - (1.0f - z->g) * exp2fx(-up * (1.0f / 12.0f));
    *t60 = z->t60 * exp2fx(-up * (1.0f / 24.0f));
}

void smp_read(const SmpZone *z, SmpR *r, uint32_t inc, f32 gain, f32 *out, int n, int add)
{
    const uint8_t *d = smp_data + z->off;
    uint32_t len = z->len, loop = z->loop, frac = r->frac & 0xFFFFu, idx = r->idx;
    f32 a = r->w[0], b = r->w[1], c = r->w[2], e = r->w[3], pre = z->pre;
    int i;
    if (inc > (8u << 16))                        /* (no zone is played that fast) */
        inc = 8u << 16;
    if (!(absf(a) < 64.0f && absf(b) < 64.0f && absf(c) < 64.0f && absf(e) < 64.0f))
        a = b = c = e = 0.0f;                    /* (not a reader's numbers) */
    for (i = 0; i < n; i++) {
        f32 t, x;
        frac += inc;
        while (frac >= 65536u) {                 /* take up the next stored sample (eight at most) */
            frac -= 65536u;
            a = b;
            b = c;
            c = e;
            if (idx >= len)
                idx = loop;
            if (idx < len)
                e = smp_mu[d[idx++]] + pre * e;
            else {                               /* past a plucked zone's end */
                e *= pre;
                if (absf(e) < 1.0e-6f)
                    e = 0.0f;
            }
        }
        t = (f32)frac * (1.0f / 65536.0f);
        /* a cubic through the four stored samples around it */
        x = b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - e + t * (3.0f * (b - c) + e - a)));
        if (add)
            out[i] += x * gain;
        else
            out[i] = x * gain;
    }
    r->frac = frac;
    r->idx = idx;
    r->w[0] = a;
    r->w[1] = b;
    r->w[2] = c;
    r->w[3] = e;
}
