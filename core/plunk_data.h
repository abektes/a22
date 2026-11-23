/* h1 - PLUNK's hits. Written by tools/samples/drums.py: do not edit. */
#pragma once
#include <stdint.h>

enum {
    VZ_GONG,       /* a gong */
    VZ_BELL,       /* a Nepalese hand bell */
    VZ_ZILL,       /* finger cymbals */
    VZ_DUM,        /* a darbuka, the low stroke */
    VZ_TEK,        /* a darbuka, the rim stroke */
    VZ_FRAME,      /* a frame drum */
    VZ_LOG,        /* a slit drum */
    VZ_BALA,       /* a balafon bar */
    VZ_SHAKER,     /* a shaker */
    VZ_AGOGO,      /* an agogo bell */
    VZ_TUMBA,      /* a tumba, the low conga */
    VZ_QUINTO,     /* a quinto, the high conga */
    VZ_BONGO,      /* a bongo */
    VZ_CAJON,      /* a cajon */
    VZ_CLAP,       /* hands clapping */
    VZ_CABASA,     /* a cabasa */
    VZ_CLAVE,      /* claves */
    VZ_COWBELL,    /* a cowbell */
    VZ_TAMB,       /* a tambourine */
    VZ_TIMP,       /* a kettledrum */
    VZ_BASSDRUM,   /* a concert bass drum */
    VZ_SNARE,      /* a rope-tension snare drum */
    VZ_STICK,      /* the same drum's rim */
    VZ_CYMBAL,     /* a suspended cymbal */
    VZ_TRIANGLE,   /* a triangle */
    VZ_WOODBLOCK,  /* a woodblock */
    VZ_SLAP,       /* a slapstick */
    VZ_BRAKE,      /* a brake drum, hammered */
    VZ_ANVIL,      /* an anvil */
    VZ_HAT,        /* a hi-hat, closed */
    VZ_GLOCK,      /* a glockenspiel bar */
    VZ_MBIRA,      /* a kalimba tine */
    VZ_CHIME,      /* a hand chime */
    VZ_KICK,       /* a kick drum, snares off (kick mic) */
    VZ_XSTICK,     /* a snare's cross-stick (snare mic) */
    VZ_SNARE2,     /* a snare drum, centre (snare mic) */
    VZ_TOMLO,      /* a low tom, hand-muted */
    VZ_TOMHI,      /* a high tom, centre */
    VZ_OHAT,       /* a hi-hat, open */
    VZ_RIDE,       /* a ride cymbal */
    VZ_MSNARE,     /* a marching snare drum */
    VZ_MRIM,       /* the same drum, a rimshot */
    VZ_TENOR,      /* a tenor drum, the lower */
    VZ_TENORH,     /* a tenor drum, the higher */
    VZ_CLASHS,     /* clash cymbals, held short */
    VZ_CLASH,      /* clash cymbals, let ring */
    VZ_VHAT,       /* a hi-hat, closed */
    VZ_N
};

typedef struct PlunkZone {
    uint32_t off;                /* into plunk_data: two samples to a byte, the low four bits first */
    uint16_t len, loop;          /* samples; a ringing hit goes round from loop to the end, another has loop == len */
    uint16_t rate;               /* Hz it is stored at */
    int16_t lpred;               /* the decoder as it was at loop, to go round with */
    uint8_t idx0, lidx;          /* its step at the start, and at loop */
    float tau;                   /* seconds: how it dies away, which was taken out of what is stored */
    float gain;                  /* makes every hit as loud as the next */
} PlunkZone;

extern const PlunkZone plunk_zones[VZ_N];
extern const uint8_t plunk_data[];
extern const uint16_t plunk_steps[89];
