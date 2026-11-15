/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's text lint, for both screens, after a draw (and_ui.c's and_draw; the text it drew is in and_fb.c's
 * and_text_log, kept when a test defines AND_LINT before including web/andamento/sim.c). The FM-1's Mono lint
 * (tests/ui_render.c) at one pixel a cell, for the page screen as the dial screen's own lint (tests/and_ui_test.c)
 * does for the cells:
 *   cut       no string drawn shorter than it is (and_text's character limit)
 *   inside    every string's box on its screen
 *   apart     two strings on the same rows with at least one clear column between them (DEPTHMUTE, T5SCL)
 *   header    the page screen's header text clear of the 8 tracks' marks (x 88 on, the header's rows 0..8)
 * The includer defines CHECK (the tests' own) before including this. */

#define AL_MARKS_X 88                            /* (and_ui.c's and_head: the 8 tracks' marks from here) */

static void and_lint_text(const char *where)
{
    uint32_t i, j;
    for (i = 0; i < and_text_logn; i++) {
        const char *scr = and_text_log[i].scr == AND_SCR_DIAL ? "dials" : "page";
        int32_t x = and_text_log[i].x, y = and_text_log[i].y, w = and_text_log[i].w, h = and_text_log[i].h;
        CHECK(!and_text_log[i].cut, "%s, %s: \"%s\" cut", where, scr, and_text_log[i].s);
        CHECK(x >= 0 && y >= 0 && x + w <= AND_W && y + h <= AND_H, "%s, %s: \"%s\" off the screen (%d,%d %dx%d)",
              where, scr, and_text_log[i].s, x, y, w, h);
        if (and_text_log[i].scr == AND_SCR_PAGE && y < 9)
            CHECK(x + w < AL_MARKS_X - 1, "%s, page: \"%s\" runs into the tracks' marks (to x %d)", where,
                  and_text_log[i].s, x + w - 1);
        for (j = i + 1u; j < and_text_logn; j++) {
            int32_t x2 = and_text_log[j].x, y2 = and_text_log[j].y, w2 = and_text_log[j].w, h2 = and_text_log[j].h;
            if (and_text_log[j].scr != and_text_log[i].scr || y + h <= y2 || y2 + h2 <= y)
                continue;                        /* (other screen, or no rows in common) */
            if (x == x2 && y == y2 && !strcmp(and_text_log[i].s, and_text_log[j].s))
                continue;                        /* (the same string drawn again in place: a bold or a redraw) */
            CHECK(x + w < x2 || x2 + w2 < x, "%s, %s: \"%s\" and \"%s\" touch", where, scr, and_text_log[i].s,
                  and_text_log[j].s);
        }
    }
}
