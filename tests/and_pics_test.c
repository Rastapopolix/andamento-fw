/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's generator pictures (platform/andamento/and_pics.c): what the page screen's picture shows on GEN, through
 * the virtual prototype's own build (web/andamento/sim.c, at 48 kHz).
 *   and_pics_test [dir]  (run by tests/run_tests.sh; exit 1 on a failed check; with a dir, the page screen with each
 *                        generator as dir/ppm/pic_*.ppm, for tests/oled_render.py)
 * Checks, for every generator on track 1 (stopped, and after 3 s playing) and every one tracks 5..8 can run:
 *   drawn     the picture lights something (dim at least: an empty pattern, ANSWER before it hears a phrase)
 *   inside    nothing outside the picture's rows (11..51): its header and tabs are never drawn over
 *   GEN       the page screen on GEN shows it (the 8 tracks' overview only elsewhere) */
#include "../web/andamento/sim.c"

#include <stdio.h>

static uint32_t nfail;
#define CHECK(c, ...) do { if (!(c)) { nfail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static const char *dir;
static void run_ms(uint32_t ms) { while (ms--) web_ms(); }
static void btn(uint32_t b) { web_button(b, 1); web_button(b, 0); }

static void dump(const char *name)
{
    char p[256];
    FILE *f;
    uint32_t i;
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

int main(int argc, char **argv)
{
    const param_desc_t *m;
    int32_t mode;
    uint32_t g;
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

    for (g = AE_OFF + 1u; g < AE_GENS; g++) {   /* tracks 5..8: track 5 with each generator it can run */
        ae_set(AE_GEN, 0, (int32_t)g);
        btn(AB_PLAY);
        run_ms(2000);
        snprintf(where, sizeof where, "T5 %s", AE_GEN_NAME[g]);
        alone(NTRK, where);
        and_select(NTRK);
        and_ui.page = AB_GEN;
        and_ui.hot_ms = 0;
        web_screen();
        snprintf(where, sizeof where, "t5_%s", AE_GEN_NAME[g]);
        for (uint32_t k = 0; where[k]; k++)
            if (where[k] >= 'A' && where[k] <= 'Z') where[k] = (char)(where[k] + 32);
        dump(where);
        and_select(0);
        btn(AB_STOP);
        run_ms(50);
    }
    ae_set(AE_GEN, 0, AE_OFF);
    CHECK(!and_pic_gen(NTRK), "T5 OFF: no picture (the overview instead)");

    if (nfail) {
        printf("and_pics: %u failed\n", nfail);
        return 1;
    }
    printf("and_pics: all checks passed\n");
    return 0;
}
