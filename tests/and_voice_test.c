/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's shared voice stage (firmware/src/vstage.c: FILTER, DRIVE and AMP after every engine) on the PC, through
 * the virtual prototype's own build (web/andamento/sim.c, at 48 kHz). A note on track 1, rendered with track_render
 * as the mixer does, measured. (That the stage at its defaults changes nothing is the golden renders' job: regress
 * renders every engine with it OFF against tests/golden_48k.txt.)
 *   and_voice_test   (run by tests/and_tests.sh; exit 1 on a failed check)
 * Checks:
 *   LP         a low CUT darkens the note (less of its energy in the highs) against OFF; CUT turned up brightens it
 *   HP, BP     a high-pass at a high CUT takes most of a low note's level away; a band-pass sounds and differs from LP
 *   LP4        steeper than LP at the same CUT (darker still)
 *   RES        full resonance on a sweep stays finite and bounded (no runaway, no NaN)
 *   ENV        ENV TO FILTER moves the shared filter (brighter with the envelope up)
 *   KEY        key tracking brightens a high note at a low CUT
 *   DRIVE      DRIVE 100 lowers the crest factor (peak over RMS): the peaks rounded off
 *   VEL        VEL 100 % (the default): a soft note quieter than a hard one; VEL 0 %: the same level
 *   engines    every engine of the build plays through the stage (LP4, RES, DRIVE on): sound, finite, bounded
 *   project    the stage's settings go through the project file (AV_PID) and come back; NEW resets them */
#include "../web/andamento/sim.c"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t nfail;
#define CHECK(c, ...) do { if (!(c)) { nfail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define NB 600u                                 /* blocks: 0.4 s at 48 kHz */
static int32_t buf[NB * CTL];

typedef struct { double rms, hf, peak; } meas_t;   /* hf: the RMS of the second difference over the RMS (brightness) */

/* track 1 on engine eng (switched only when another: the settings made after the switch stay), cleared, one note (note, vel) held for NB blocks; the last 3/4 measured */
static meas_t play(uint32_t eng, uint32_t note, uint32_t vel)
{
    track_t *t = &trk[0];
    uint32_t b, i, i0 = NB * CTL / 4u;
    double s2 = 0, d2 = 0, pk = 0;
    meas_t m;
    for (i = 0; i < NVOICE; i++)
        t->v[i].active = t->v[i].gate = 0, t->v[i].stage = 0, t->v[i].env = t->v[i].env_out = 0;
    if (t->eng_req != eng)                      /* (a switch loads the engine's preset: its ENV and LFO amounts) */
        set_engine_of(t, eng);
    t->engine = t->eng_req;
    t->xf_on = 0;
    for (i = 0; i < 4u; i++)
        track_render(t, buf, CTL);
    trk_note_on(t, note, vel);
    for (b = 0; b < NB; b++)
        track_render(t, buf + b * CTL, CTL);
    trk_note_off(t, note);
    for (i = i0; i < NB * CTL; i++) {
        double x = buf[i], d = x - 2.0 * buf[i - 1u] + buf[i - 2u];   /* (the second difference: the highs weigh) */
        s2 += x * x;
        d2 += d * d;
        if (fabs(x) > pk)
            pk = fabs(x);
    }
    m.rms = sqrt(s2 / (NB * CTL - i0));
    m.hf = s2 > 0 ? sqrt(d2 / s2) : 0;
    m.peak = pk;
    return m;
}

static uint32_t eng_named(const char *n)
{
    uint32_t i;
    for (i = 0; i < NENGINES; i++)
        if (eng_ok(i) && !strcmp(N_ENGNAME[i], n))
            return i;
    return NENGINES;
}
static int finite_bounded(void)
{
    uint32_t i;
    for (i = 0; i < NB * CTL; i++)
        if (buf[i] > 4 * 32768 * 8 || buf[i] < -4 * 32768 * 8)
            return 0;
    return 1;
}

int main(void)
{
    uint32_t trio, k;
    meas_t off, lp, m;
    web_boot();
    trio = eng_named("TRIO");
    CHECK(trio < NENGINES, "TRIO in this build");
    set_engine_of(&trk[0], trio);
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_E5] = 127;                       /* (TRIO's own CUT wide open: a bright source for the shared filter) */
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_LD_FLT] = 0;

    /* LP */
    vs_defaults(0);
    off = play(trio, 48, 100);
    CHECK(off.rms > 100, "TRIO plays (rms %.0f)", off.rms);
    vs_set(0, VS_TYPE, VS_LP);
    vs_set(0, VS_CUT, 30);
    lp = play(trio, 48, 100);
    CHECK(lp.hf < off.hf * 0.6, "LP at CUT 30 darkens the note (hf %.3f against OFF %.3f)", lp.hf, off.hf);
    vs_set(0, VS_CUT, 90);
    m = play(trio, 48, 100);
    CHECK(m.hf > lp.hf * 1.3, "CUT turned up brightens it (hf %.3f against %.3f)", m.hf, lp.hf);

    /* LP4, BP, HP */
    vs_set(0, VS_CUT, 50);
    lp = play(trio, 48, 100);
    vs_set(0, VS_TYPE, VS_LP4);
    m = play(trio, 48, 100);
    CHECK(m.hf < lp.hf, "LP4 is steeper than LP (hf %.3f against %.3f)", m.hf, lp.hf);
    vs_set(0, VS_TYPE, VS_HP);
    vs_set(0, VS_CUT, 110);
    m = play(trio, 36, 100);
    off = (vs_set(0, VS_TYPE, VS_OFF), play(trio, 36, 100));
    CHECK(m.rms < off.rms * 0.5, "HP at a high CUT takes a low note's level away (rms %.0f against %.0f)", m.rms, off.rms);
    vs_set(0, VS_TYPE, VS_BP);
    vs_set(0, VS_CUT, 60);
    m = play(trio, 48, 100);
    vs_set(0, VS_TYPE, VS_LP);
    lp = play(trio, 48, 100);
    CHECK(m.rms > 10 && fabs(m.hf - lp.hf) > 0.01, "BP sounds and differs from LP (hf %.3f, %.3f)", m.hf, lp.hf);

    /* RES: a sweep at full resonance stays bounded */
    vs_set(0, VS_RES, 127);
    for (k = 0; k <= 127; k += 16) {
        vs_set(0, VS_CUT, (int32_t)k);
        play(trio, 48, 127);
        CHECK(finite_bounded(), "RES 127 at CUT %u: bounded", k);
    }
    vs_set(0, VS_RES, 0);

    /* ENV TO FILTER moves the shared filter */
    vs_set(0, VS_TYPE, VS_LP);
    vs_set(0, VS_CUT, 20);
    lp = play(trio, 48, 100);
    trk[0].p[P_ED_FLT] = 63;
    m = play(trio, 48, 100);
    CHECK(m.hf > lp.hf * 1.2, "ENV TO FILTER opens the shared filter (hf %.3f against %.3f)", m.hf, lp.hf);
    trk[0].p[P_ED_FLT] = 0;

    /* KEY */
    vs_set(0, VS_CUT, 40);
    lp = play(trio, 84, 100);
    vs_set(0, VS_KEY, 100);
    m = play(trio, 84, 100);
    CHECK(m.hf > lp.hf * 1.2, "KEY 100 %% brightens a high note (hf %.3f against %.3f)", m.hf, lp.hf);
    vs_set(0, VS_KEY, 0);

    /* DRIVE */
    vs_set(0, VS_TYPE, VS_OFF);
    off = play(trio, 48, 127);
    vs_set(0, VS_DRIVE, 100);
    m = play(trio, 48, 127);
    CHECK(m.peak / m.rms < off.peak / off.rms * 0.9, "DRIVE 100 lowers the crest factor (%.2f against %.2f)",
          m.peak / m.rms, off.peak / off.rms);
    CHECK(m.rms > off.rms * 0.3 && m.rms < off.rms * 3.0, "DRIVE keeps the level in range (rms %.0f against %.0f)",
          m.rms, off.rms);
    vs_set(0, VS_DRIVE, 0);

    /* VEL */
    {
        meas_t hard = play(trio, 48, 110), soft = play(trio, 48, 30);   /* (under 111: no accent) */
        CHECK(soft.rms < hard.rms * 0.4, "VEL 100 %%: a soft note quieter (%.0f against %.0f)", soft.rms, hard.rms);
        vs_set(0, VS_VEL, 0);
        hard = play(trio, 48, 110), soft = play(trio, 48, 30);
        CHECK(fabs(soft.rms - hard.rms) < hard.rms * 0.02, "VEL 0 %%: the same level (%.0f against %.0f)", soft.rms,
              hard.rms);
        vs_set(0, VS_VEL, 100);
    }

    /* every engine through the stage */
    vs_set(0, VS_TYPE, VS_LP4);
    vs_set(0, VS_CUT, 70);
    vs_set(0, VS_RES, 100);
    vs_set(0, VS_DRIVE, 50);
    for (k = 0; k < NENG_SHOWN; k++) {
        uint32_t e = eng_vis(k);
        m = play(e, 48, 110);
        CHECK(m.peak > 0 && finite_bounded(), "%s through the stage: sound, bounded (peak %.0f)", N_ENGNAME[e], m.peak);
    }

    /* the project file */
    {
        static uint8_t f[ANDP_MAX];
        uint32_t n;
        vs_set(0, VS_TYPE, VS_BP), vs_set(0, VS_CUT, 33), vs_set(0, VS_RES, 44), vs_set(0, VS_DRIVE, 55);
        vs_set(0, VS_KEY, 66), vs_set(0, VS_VEL, 77), vs_set(3, VS_TYPE, VS_HP);
        n = andp_save(f, sizeof f);
        CHECK(n > 0, "the project saves");
        andp_defaults();
        CHECK(vs_get(0, VS_TYPE) == VS_OFF && vs_get(0, VS_VEL) == 100 && vs_get(3, VS_TYPE) == VS_OFF,
              "NEW: the stage at its defaults");
        CHECK(andp_load(f, n) == ANDP_OK, "the project loads");
        CHECK(vs_get(0, VS_TYPE) == VS_BP && vs_get(0, VS_CUT) == 33 && vs_get(0, VS_RES) == 44 &&
              vs_get(0, VS_DRIVE) == 55 && vs_get(0, VS_KEY) == 66 && vs_get(0, VS_VEL) == 77 &&
              vs_get(3, VS_TYPE) == VS_HP && vs_get(1, VS_TYPE) == VS_OFF,
              "the stage's settings come back from the file");
    }

    if (nfail) {
        printf("and_voice: %u failed\n", nfail);
        return 1;
    }
    printf("and_voice: all checks passed\n");
    return 0;
}
