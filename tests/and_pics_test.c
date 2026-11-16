/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's generator pictures (platform/andamento/and_pics.c): what the page screen's picture shows on GEN, through
 * the virtual prototype's own build (web/andamento/sim.c, at 48 kHz).
 *   and_pics_test [dir]  (run by tests/run_tests.sh; exit 1 on a failed check; with a dir, the page screen with each
 *                        generator as dir/ppm/pic_*.ppm, for tests/oled_render.py)
 * Checks, for every generator on track 1 (stopped, and after 3 s playing) and every one tracks 5..8 can run:
 *   drawn     the picture lights something (dim at least: an empty pattern, ANSWER before it hears a phrase)
 *   inside    nothing outside the picture's rows (11..51): its header and tabs are never drawn over
 *   GEN       the page screen on GEN shows it (the 8 tracks' overview only elsewhere)
 *   QUAD      drawn for this screen (and_pic_quad): a clear row between its four rows, the track shown boxed
 * and the pages' own: ENV on AMP, FM6's EG RATE / LVL and PITCH EG / LV (a rate, a level, the operator changing it),
 * FILTER (the shared filter: none while OFF, the cutoff moving it, each TYPE its own shape, on any engine), LFO, PIECE */
#define AND_LINT 1                               /* and_fb.c keeps the text drawn (and_lint.h) */
#include "../web/andamento/sim.c"

#include <stdio.h>

static uint32_t nfail;
#define CHECK(c, ...) do { if (!(c)) { nfail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#include "and_lint.h"

static const char *dir;
static void run_ms(uint32_t ms) { while (ms--) web_ms(); }
static void btn(uint32_t b) { web_button(b, 1); web_button(b, 0); }

static void dump(const char *name)
{
    char p[256];
    FILE *f;
    uint32_t i;
    and_lint_text(name);                         /* (the text of the draw just made: and_lint.h) */
    if (!dir)
        return;
    snprintf(p, sizeof p, "%s/ppm/pic_%s.ppm", dir, name);
    for (i = 0; p[i]; i++)
        if (p[i] == ' ') p[i] = '_';
    if (!(f = fopen(p, "wb")))
        return;
    fprintf(f, "P6\n%d %d\n255\n", AND_W, AND_H);
    for (i = 0; i < AND_W * AND_H; i++) {
        uint32_t l = and_fbs[AND_SCR_PAGE][0][i] & 15u;
        fputc((int)(255u * l / 15u), f);
        fputc((int)(208u * l / 15u), f);
        fputc((int)(32u * l / 15u), f);
    }
    fclose(f);
}

/* the picture of track tr alone on a cleared page screen: lit, and inside its rows */
static void alone(uint32_t tr, const char *where)
{
    uint32_t x, y, peak = 0, out = 0;
    and_target(AND_SCR_PAGE);
    and_clear();
    and_pic_gen(tr);
    for (y = 0; y < AND_H; y++)
        for (x = 0; x < AND_W; x++) {
            uint32_t l = and_fbs[AND_SCR_PAGE][y][x];
            if (y < AND_PIC_Y || y > AND_PIC_Y + 40u)
                out += l != 0;
            else if (l > peak)
                peak = l;
        }
    CHECK(peak >= PC_DIM, "%s: the picture drawn (peak %u)", where, peak);   /* (an empty pattern, ANSWER before a phrase: dim) */
    CHECK(!out, "%s: %u pixels outside the picture's rows", where, out);
}

/* page p's sub-page with this tab for track 1 (and_nsubs: none) */
static uint32_t sub_of(uint32_t p, const char *tab)
{
    uint32_t k, n = and_nsub(p);
    for (k = 0; k < n; k++)
        if (!strcmp(and_subs[k].tab, tab))
            return k;
    return and_nsubs;
}
/* page p's sub-page sub's own picture for track 1 alone, as alone(): 1 when the page has one, then lit and inside its
 * rows; its frame on the whole page screen saved as name */
static int page_alone(uint32_t p, uint32_t sub, const char *where, const char *name)
{
    uint32_t x, y, peak = 0, out = 0, n = and_nsub(p);
    int has;
    and_target(AND_SCR_PAGE);
    and_clear();
    has = sub < n && and_pic_page(p, sub, 0);
    for (y = 0; y < AND_H; y++)
        for (x = 0; x < AND_W; x++) {
            uint32_t l = and_fbs[AND_SCR_PAGE][y][x];
            if (y < AND_PIC_Y || y > AND_PIC_Y + 40u)
                out += l != 0;
            else if (l > peak)
                peak = l;
        }
    if (has) {
        CHECK(peak >= PC_DIM, "%s: the picture drawn (peak %u)", where, peak);
        CHECK(!out, "%s: %u pixels outside the picture's rows", where, out);
    }
    and_ui.page = (uint8_t)p;
    and_ui.sub[p] = (uint8_t)sub;
    and_ui.hot_ms = 0;
    web_screen();
    dump(name);
    return has;
}
static uint32_t eng_named(const char *n)        /* an engine by its name (NENGINES: not in this build) */
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (!strcmp(N_ENGNAME[e], n))
            return e;
    return NENGINES;
}
static uint32_t pic_sum(void)                     /* the picture's rows, summed: did it change */
{
    uint32_t x, y, h = 0;
    for (y = AND_PIC_Y; y <= AND_PIC_Y + 40u; y++)
        for (x = 0; x < AND_W; x++)
            h = h * 31u + and_fbs[AND_SCR_PAGE][y][x];
    return h;
}

int main(int argc, char **argv)
{
    const param_desc_t *m;
    int32_t mode;
    uint32_t g, trio;
    char where[48];
    dir = argc > 1 ? argv[1] : 0;
    web_boot();
    run_ms(200);
    and_select(0);
    btn(AB_GEN);
    m = pid_desc(AND_PID_MODE, 0);
    CHECK(m && m->names, "GEN's MODE has names");
    if (!m || !m->names)
        return 1;
    for (mode = m->min; mode <= m->max; mode++) {
        const char *name = m->names[mode - m->min];
        if (!name[0] || pid_set(AND_PID_MODE, 0, mode), pid_get(AND_PID_MODE, 0) != mode)
            continue;                            /* (an alias, or not in this build) */
        snprintf(where, sizeof where, "T1 %s", name);
        alone(0, where);
        if (!song.playing)
            btn(AB_PLAY);
        run_ms(3000);
        snprintf(where, sizeof where, "T1 %s playing", name);
        alone(0, where);
        if (!strcmp(name, "QUAD")) {             /* (alone() left the picture drawn) */
            uint32_t r, x, lit;
            for (r = 0; r + 1u < 4u; r++) {               /* (four rows: tracks 1..4) */
                for (lit = 0, x = 0; x < AND_W; x++)
                    lit += and_fbs[AND_SCR_PAGE][QP_Y + r * QP_DY + QP_H + 1u][x] != 0;
                CHECK(!lit, "QUAD: rows %u and %u touch (%u pixels between them)", r + 1u, r + 2u, lit);
            }
            CHECK(and_fbs[AND_SCR_PAGE][QP_Y][1] == PC_INK, "QUAD: track 1's row boxed");
        }
        and_ui.hot_ms = 0;
        web_screen();
        CHECK(and_ui.page == AB_GEN, "%s: on GEN", where);
        snprintf(where, sizeof where, "t1_%s", name);
        for (g = 0; where[g]; g++)
            if (where[g] >= 'A' && where[g] <= 'Z') where[g] = (char)(where[g] + 32);
        dump(where);
        btn(AB_STOP);
        run_ms(50);
    }
    pid_set(AND_PID_MODE, 0, 0);

    for (g = 0; g < 4u; g++) {                   /* tracks 5..8 are the core's: track 5 with each of their generators */
        static const uint8_t GEN[4] = {TESS_ENG_TURING, TESS_ENG_EUCLID, TESS_ENG_MARKOV, TESS_ENG_AWAKE};
        static const char *const NAME[4] = {"turing", "euclid", "markov", "awake"};
        pid_set(AND_PID_MODE, 4, GEN[g]);
        btn(AB_PLAY);
        run_ms(2000);
        snprintf(where, sizeof where, "T5 %s", NAME[g]);
        alone(4, where);
        and_select(4);
        and_ui.page = AB_GEN;
        and_ui.hot_ms = 0;
        web_screen();
        snprintf(where, sizeof where, "t5_%s", NAME[g]);
        dump(where);
        and_select(0);
        btn(AB_STOP);
        run_ms(50);
    }
    pid_set(AND_PID_MODE, 4, TESS_ENG_TURING);

    /* the pages' own: ENV on AMP (an engine with the shared envelope; FM6 has none), LFO, PIECE */
    and_select(0);
    pid_set(AND_PID_MODE, 0, 0);
    set_engine_of(&trk[0], ENGI_ORACLE);
    {
        uint32_t amp = sub_of(AB_SOUND, "AMP"), h0;
        CHECK(page_alone(AB_SOUND, amp, "ORACLE AMP", "env"), "ORACLE AMP: the envelope's picture");
        h0 = pic_sum();
        if (amp < and_nsubs && and_subs[amp].c[0] != PID_NONE) {   /* (the attack: its patch's on ORACLE) */
            const param_desc_t *d = and_desc(and_subs[amp].c[0], 0);
            and_set(and_subs[amp].c[0], 0, and_get(and_subs[amp].c[0], 0) > (d->min + d->max) / 2 ? d->min : d->max);
        }
        page_alone(AB_SOUND, amp, "ORACLE AMP, attack turned", "env_attack");
        CHECK(pic_sum() != h0, "ORACLE AMP: the attack turned changes the envelope");
        set_engine_of(&trk[0], ENGI_FM6);
        CHECK(!page_alone(AB_SOUND, sub_of(AB_SOUND, "AMP"), "FM6 AMP", "env_fm6"),
              "FM6 AMP: no shared envelope, no picture (the overview instead)");
    }
    /* FM6's own envelopes: EG RATE / LVL (the operator OP picks) and PITCH EG / LV */
    {
        uint32_t k, n = and_nsub(AB_SOUND), eg = n, peg = n, h0;
        for (k = 0; k < n; k++)
            if (and_is_ap(and_subs[k].c[0])) {
                uint32_t gr = PAGES[(and_subs[k].c[0] - AP_PID) / 4u].graph;
                if (gr == GR_FMEG && eg == n) eg = k;
                if (gr == GR_FMPEG && peg == n) peg = k;
            }
        CHECK(eg < n && peg < n, "FM6: its EG and PITCH EG sub-pages (%u, %u of %u)", eg, peg, n);
        CHECK(page_alone(AB_SOUND, eg, "FM6 EG", "fm6_eg"), "FM6 EG RATE / LVL: the envelope's picture");
        h0 = pic_sum();
        and_set(and_subs[eg].c[0], 0, and_get(and_subs[eg].c[0], 0) > 50 ? 10 : 90);   /* (R1) */
        page_alone(AB_SOUND, eg, "FM6 EG, R1 turned", "fm6_eg_r1");
        CHECK(pic_sum() != h0, "FM6 EG: R1 turned changes the envelope");
        h0 = pic_sum();
        and_set(and_subs[eg].c[5], 0, and_get(and_subs[eg].c[5], 0) > 50 ? 10 : 90);   /* (L2) */
        page_alone(AB_SOUND, eg, "FM6 EG, L2 turned", "fm6_eg_l2");
        CHECK(pic_sum() != h0, "FM6 EG: L2 turned changes the envelope");
        h0 = pic_sum();
        fm6_opsel = 1;                           /* (OP2: its own envelope, R1 and L2 as the preset has them) */
        page_alone(AB_SOUND, eg, "FM6 EG OP2", "fm6_eg_op2");
        CHECK(pic_sum() != h0, "FM6 EG: OP2's envelope, not OP1's");
        fm6_opsel = 0;
        CHECK(page_alone(AB_SOUND, peg, "FM6 PITCH EG", "fm6_peg"), "FM6 PITCH EG / LV: the envelope's picture");
    }
    trio = eng_named("TRIO");
    CHECK(trio < NENGINES, "TRIO in this build");
    /* FILTER: the shared voice stage's response (vstage.c): none while its TYPE is OFF, on every engine once on; CUT
     * moves it, each TYPE its own shape */
    set_engine_of(&trk[0], trio);
    {
        uint32_t filt = sub_of(AB_SOUND, "FILT"), h0, k;
        vs_set(0, VS_TYPE, VS_OFF);
        CHECK(!page_alone(AB_SOUND, filt, "FILTER OFF", "filter_off"), "FILTER OFF: no picture (the overview instead)");
        vs_set(0, VS_TYPE, VS_LP);
        CHECK(page_alone(AB_SOUND, filt, "TRIO FILTER", "filter"), "FILTER LP: the response's picture");
        CHECK(filt < and_nsubs && and_subs[filt].c[0] == AV_C(VS_TYPE) && and_subs[filt].c[1] == AV_C(VS_CUT),
              "FILTER: TYPE and CUT on encoders 1 and 2");
        h0 = pic_sum();
        vs_set(0, VS_CUT, 20);
        page_alone(AB_SOUND, filt, "FILTER, cutoff turned", "filter_cut");
        CHECK(pic_sum() != h0, "FILTER: the cutoff turned moves the response");
        {
            static const uint32_t TY[2] = {VS_BP, VS_HP};
            static const char *const TN[2] = {"filter_bp", "filter_hp"};
            for (k = 0; k < 2u; k++) {
                h0 = pic_sum();
                vs_set(0, VS_TYPE, TY[k]);
                page_alone(AB_SOUND, filt, TN[k] + 7, TN[k]);
                CHECK(pic_sum() != h0, "FILTER: %s drawn its own way", TN[k] + 7);
            }
        }
        set_engine_of(&trk[0], ENGI_PHYS);
        CHECK(page_alone(AB_SOUND, sub_of(AB_SOUND, "FILT"), "PHYS FILTER", "filter_phys"),
              "PHYS FILTER: the shared filter on an engine with no filter of its own");
        set_engine_of(&trk[0], ENGI_ORACLE);
        CHECK(page_alone(AB_SOUND, sub_of(AB_SOUND, "FILT"), "ORACLE FILTER", "filter_oracle"),
              "ORACLE FILTER: the shared filter's response");
        vs_defaults(0);
    }
    set_engine_of(&trk[0], trio);
    btn(AB_PLAY);
    run_ms(1500);
    CHECK(page_alone(AB_MOD, 0, "LFO", "lfo"), "MOD's LFO: the LFO's picture");
    btn(AB_STOP);
    run_ms(50);
    tessera.pl_length = 0;
    CHECK(page_alone(AB_PIECE, 0, "PIECE FREE", "piece_free"), "PIECE: the plan's picture, FREE");
    tessera.pl_length = 4;
    page_alone(AB_PIECE, 0, "PIECE 4", "piece");
    btn(AB_PLAY);
    run_ms(3000);
    page_alone(AB_PIECE, 0, "PIECE 4 playing", "piece_playing");
    btn(AB_STOP);
    run_ms(50);

    if (nfail) {
        printf("and_pics: %u failed\n", nfail);
        return 1;
    }
    printf("and_pics: all checks passed\n");
    return 0;
}
