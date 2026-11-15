/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's pictures, the generators' first: what the page screen's picture (rows 11..51, 128 x 41) shows on GEN.
 * Each is the FM-1's Mono picture of that generator (Tessera firmware/src/ui_mono.c, From Tessera 81aa730f), copied
 * here with its drawing calls renamed (mo_ -> pc_), so Andamento keeps Tessera's pictures while the FM-1's UI can be
 * trimmed out of this tree later (andamento/FORK_PLAN.md). Included by and_ui.c after and_fb.c; reads only the core
 * (tessera.c's generators, gen.c, answer.c, seq.c), never the FM-1's UI.
 *
 * How they fit. The FM-1 draws a picture in cells of 2 x 2 pixels, 120 wide, on its rows 52..108 (57 rows). Here a
 * cell is a pixel: the width carries over (120 of 128, 4 each side) and the rows are squeezed into 41 (PC_Y), so bars
 * keep their order and proportion, a step its place. Text keeps its 5 x 7 size, placed by its middle; a ring or a dot
 * its size squeezed. Euclid's ring is the one shape drawn round: its x radius is squeezed too. QUAD is drawn for this
 * screen instead (and_pic_quad), as norns' initenere draws its grid: the FM-1's rows squeezed would touch.
 *
 *   pictures  the pattern (no generator, or a step generator), Turing, Euclid, Grids, Markov, Morph, Bounce (and its
 *             FALL), Awake, Poly, Quad, Constellation, Series, Chord, Answer, and STEP as seed
 *   tracks 5..8  their generator (and_ext.c runs Tessera's own: Turing, Euclid, Markov, Awake, Poly, Series)
 *   inks      the FM-1's ink 15, its dim 5, the background 0; a track's own ink (MOSAIC's) is the ink (one ink here)
 *
 * The pages' own (and_pic_page): ENV on SOUND's AMP, LFO on MOD's first, PIECE on PIECE's first, the FM-1's too.
 *
 * Not yet: FILTER's response, FM6's own envelopes (EG RATE / LVL), the FM-1's layers drawn over a picture (QUAD's
 * column, the keys' edits). */

#define PC_BG 0u
#define PC_INK 15u
#define PC_DIM 5u
#define PC_HOT 15u
#define PC_TRK(k) PC_INK
static uint32_t pc_tdim(uint32_t k) { (void)k; return PC_DIM; }

#define PC_Y0 52                                 /* the FM-1's picture rows .. */
#define PC_Y1 109
#define PC_SQ_N 41                               /* .. into AND_PIC_Y .. AND_PIC_Y + 40: rows squeezed 41 / 57 */
#define PC_SQ_D 57
static int32_t pc_y(int32_t y) { return AND_PIC_Y + ((y - PC_Y0) * PC_SQ_N + PC_SQ_D / 2) / PC_SQ_D; }
static int32_t pc_x(int32_t x) { return x + 4; }

static void pc_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    int32_t y0 = pc_y(y), y1 = pc_y(y + h);
    if (w > 0 && h > 0)
        and_fill(pc_x(x), y0, w, y1 > y0 ? y1 - y0 : 1, c);
}
static void pc_px(int32_t x, int32_t y, uint32_t c) { and_px(pc_x(x), pc_y(y), c); }
static void pc_hl(int32_t x, int32_t y, int32_t w, uint32_t c) { pc_fill(x, y, w, 1, c); }
static void pc_vl(int32_t x, int32_t y, int32_t h, uint32_t c) { pc_fill(x, y, 1, h, c); }
static void pc_box(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)   /* an outline */
{
    int32_t y0 = pc_y(y), y1 = pc_y(y + h - 1);
    if (w > 0 && h > 0)
        and_rect(pc_x(x), y0, w, y1 > y0 ? y1 - y0 + 1 : 1, c);
}
static void pc_dash(int32_t x, int32_t y, int32_t w, uint32_t c)
{
    for (int32_t i = 0; i < w; i += 2)
        pc_px(x + i, y, c);
}
static void pc_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c)
{
    and_line(pc_x(x0), pc_y(y0), pc_x(x1), pc_y(y1), c);
}
static int32_t pc_r(int32_t r) { r = (r * PC_SQ_N + PC_SQ_D / 2) / PC_SQ_D; return r < 1 ? 1 : r; }
static void pc_circ(int32_t cx, int32_t cy, int32_t r, uint32_t c) { and_ring(pc_x(cx), pc_y(cy), pc_r(r), c); }
static void pc_disc(int32_t cx, int32_t cy, int32_t r, uint32_t c)
{
    int32_t x = pc_x(cx), y0 = pc_y(cy), y, w;
    r = pc_r(r);
    for (y = -r; y <= r; y++) {
        for (w = 0; (w + 1) * (w + 1) + y * y <= r * r + r; w++)
            ;
        and_fill(x - w, y0 + y, 2 * w + 1, 1, c);
    }
}
static const int8_t PC_SIN[64] = {0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125,
    122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106,
    -112, -117, -122, -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25,
    -12};
static int32_t pc_sin(int32_t a)                  /* sine of a (256 a turn), x 127 */
{
    uint32_t i = ((uint32_t)a & 255u) >> 2, f = (uint32_t)a & 3u;
    return (PC_SIN[i] * (int32_t)(4u - f) + PC_SIN[(i + 1u) & 63u] * (int32_t)f) / 4;
}
static int32_t pc_cos(int32_t a) { return pc_sin(a + 64); }
static int32_t pc_tw(const char *s) { int32_t n = (int32_t)str_len(s); return n ? n * 6 - 1 : 0; }
static int32_t pc_ty(int32_t y) { return pc_y(y + 3) - 3; }   /* a line of text by its middle */
static int32_t pc_text(const char *s, int32_t x, int32_t y, uint32_t c)
{
    return pc_x(x) + and_text(pc_x(x), pc_ty(y), s, c, 21) - 4;
}
static void pc_textc(const char *s, int32_t cx, int32_t y, uint32_t c) { pc_text(s, cx - pc_tw(s) / 2, y, c); }
static void pc_textr(const char *s, int32_t rx, int32_t y, uint32_t c) { pc_text(s, rx - pc_tw(s), y, c); }
static void pc_invc(const char *s, int32_t x, int32_t y, uint32_t c)   /* on a filled box of ink c */
{
    and_fill(pc_x(x) - 1, pc_ty(y) - 1, pc_tw(s) + 2, 9, c);
    and_text(pc_x(x), pc_ty(y), s, PC_BG, 21);
}
static void pc_inv(const char *s, int32_t x, int32_t y) { pc_invc(s, x, y, PC_INK); }

/* ---- the pictures (Tessera's, ui_mono.c) ---- */
static void pc_pic_pattern(const track_t *t)
{
    uint32_t i, len = (uint32_t)t->p[P_SLEN], rows, rh, sh;
    char b[16];
    if (len > NSTEP) len = NSTEP;
    rows = (len + 15u) / 16u;
    rh = rows <= 2u ? 22u : 12u;                        /* a row's height, its steps' */
    sh = rows <= 2u ? 9u : 7u;
    for (i = 0; i < len; i++) {
        const step_t *st = &seq_steps(t)[i];
        int32_t x = 14 + (int32_t)(i % 16u) * 33 / 5, y = 54 + (int32_t)(i / 16u * rh);
        if (i % 16u == 0) {
            fmt_int(b, (int32_t)i + 1);
            pc_text(b, 1, y + (int32_t)sh - 7, PC_DIM);
        }
        if (ak_step_on(st)) {                   /* (and_keys.c: step_on is the FM-1 UI's) */
            pc_fill(x, y, 5, (int32_t)sh, PC_INK);
            if (st->flags & SF_ACCENT) pc_fill(x + 1, y + 1, 3, 2, PC_BG);   /* an accent: a notch */
        } else if (st->time == ST_TIE) {
            pc_box(x, y + (int32_t)sh - 4, 5, 4, PC_INK);
        } else {
            pc_fill(x, y + (int32_t)sh - 2, 5, 2, PC_DIM);
        }
        if (0)
            pc_box(x - 1, y - 1, 7, (int32_t)sh + 2, PC_INK);
        if (song.playing && i == t->seq_idx)
            pc_hl(x, y + (int32_t)sh + 1, 5, PC_INK);
    }
    str_cpy(b, "LEN ", sizeof b);
    fmt_int(b + 4, (int32_t)len);
    pc_text(b, 1, 101, PC_DIM);
    if (song.playing) {
        str_cpy(b, "STEP ", sizeof b);
        fmt_int(b + 5, (int32_t)t->seq_idx % (int32_t)(len ? len : 1u) + 1);
        pc_textr(b, 118, 101, PC_INK);
    }
}
static void pc_kv(const char *k, uint32_t v, int32_t y, int inv)   /* the GEN pictures' side column */
{
    char b[8];
    fmt_int(b, (int32_t)v);
    if (v < 10u) { b[1] = b[0]; b[0] = '0'; b[2] = 0; }
    pc_text(k, 78, y, PC_DIM);
    (void)inv;                                   /* (the FM-1 boxes the knobs' values; 8 px apart here, boxes touch) */
    pc_text(b, 100, y, PC_INK);
}
static void pc_pic_euclid(const gen_euclid_t *g)
{
    int32_t cx = 42, cy = 80, r = 25;
    uint32_t n = g->steps ? g->steps : 1u, i, head = (g->step_idx + n - 1u) % n;
    char b[8];
    for (i = 0; i < n; i++) {
        int32_t a = -64 + (int32_t)(i * 256u / n), x = cx + pc_cos(a) * r * PC_SQ_N / PC_SQ_D / 127,
                y = cy + pc_sin(a) * r / 127;   /* (x by the rows' squeeze: a circle, not an oval) */
        if (gen_euclid_hit((uint8_t)i, g->steps, g->pulses, g->rot)) pc_fill(x - 2, y - 2, 5, 5, PC_INK);
        else pc_box(x - 2, y - 2, 5, 5, PC_DIM);
        if (song.playing && i == head) pc_circ(x, y, 4, PC_INK);
    }
    pc_box(cx - 7, cy - 7, 15, 15, PC_INK);
    fmt_int(b, g->pulses);
    pc_textc(b, cx + 1, cy - 3, PC_INK);
    pc_kv("LEN", g->steps, 58, 1);
    pc_kv("HIT", g->pulses, 70, 1);
    pc_kv("ROT", g->rot, 82, 1);
    pc_kv("STP", head + 1u, 94, 0);
}
static void pc_pic_turing(const gen_turing_t *g)
{
    uint32_t len = g->len ? g->len : 16u, span = g->oct_span ? g->oct_span : 1u, i;
    for (i = 0; i < 16u; i++) {                         /* the register: its loop's bits, the rest dim */
        int32_t x = 2 + (int32_t)i * 73 / 10;
        uint32_t bit = i < len ? (g->reg >> (len - 1u - i)) & 1u : 0u;
        if (i >= len) pc_box(x, 55, 6, 6, PC_DIM);
        else if (bit) pc_fill(x, 55, 6, 6, PC_INK);
        else pc_box(x, 55, 6, 6, PC_INK);
    }
    pc_hl(2, 63, (int32_t)len * 73 / 10 - 1, PC_INK);    /* the loop's length */
    for (i = 0; i < len; i++) {                         /* each step's note, as a bar (as the detailed picture) */
        uint32_t raw = ((g->reg & (len >= 16u ? 0xFFFFu : ((1u << len) - 1u))) >> (len - 1u - i)) & 0xFFu;
        uint32_t v = ((raw / 7u) % span) * 7u + raw % 7u, mx = span * 7u - 1u;
        int32_t h = 4 + (int32_t)(mx ? v * 32u / mx : 0u), x = 2 + (int32_t)i * 73 / 10;
        if (song.playing && i == len - 1u) pc_fill(x, 104 - h, 6, h, PC_INK);
        else pc_box(x, 104 - h, 6, h, PC_INK);
    }
}
static void pc_pic_grids(const gen_grids_t *g, const track_t *t)
{
    static const char *const LN[3] = {"BD", "SD", "HH"};
    uint32_t steps = gen_grids_steps(tess_steps_per_bar(t)), cur = (g->step_idx + steps - 1u) % steps, p, i;
    int32_t px = 3 + g->x * 37 / 255, py = 55 + g->y * 37 / 255;
    pc_box(2, 54, 40, 40, PC_INK);                      /* the map, its 5 x 5 patterns, the point on it */
    for (i = 1; i < 5u; i++) {
        pc_dash(3, 54 + (int32_t)i * 8, 38, PC_DIM);
        for (int32_t y = 55; y < 93; y += 2) pc_px(2 + (int32_t)i * 8, y, PC_DIM);
    }
    pc_vl(px, 55, 38, PC_INK);
    pc_hl(3, py, 38, PC_INK);
    pc_disc(px, py, 2, PC_INK);
    for (p = 0; p < 3u; p++) {                          /* the three parts: 16 steps as two rows of eight */
        int32_t y = 56 + (int32_t)p * 17;
        pc_text(LN[p], 47, y + 2, PC_DIM);
        for (i = 0; i < 16u; i++) {
            int acc = 0;
            uint32_t s = steps > 16u ? i * 2u : i;
            int32_t x = 61 + (int32_t)(i % 8u) * 7, yy = y + (i < 8u ? 0 : 7);
            if (gen_grids_hit(g, s, steps, p, 0u, &acc)) pc_fill(x, yy, 5, 5, PC_INK);
            else pc_px(x + 2, yy + 2, PC_DIM);
            if (song.playing && (steps > 16u ? cur / 2u : cur) == i) pc_box(x - 1, yy - 1, 7, 7, PC_INK);
        }
    }
}
/* Markov (as the detailed picture, ui_graph.c): the eight degrees, each bar the chance it comes next from the one
 * playing (gen_markov_row, so TENSION reshapes them); the one playing solid on a solid underline, a rest hollow on a
 * dashed one; the others outlined */
static void pc_pic_markov(const gen_markov_t *g)
{
    uint8_t row[8];
    uint32_t d, cur = g->cur_state & 7u, prev = 0;
    char b[16];
    gen_markov_row(g->tension, cur, row);
    for (d = 0; d < 8u; d++) {
        uint32_t pr = row[d] > prev ? row[d] - prev : 0u;   /* (the row is cumulative) */
        int32_t h, x = 5 + (int32_t)d * 14;
        prev = row[d];
        pr = pr > 40u ? 40u : pr;
        h = 3 + (int32_t)(pr * 37u / 40u);
        if (d == cur && g->last_hit) {
            pc_fill(x, 95 - h, 10, h, PC_INK);
            pc_fill(x, 97, 10, 2, PC_INK);
        } else if (d == cur) {
            pc_box(x, 95 - h, 10, h, PC_INK);
            pc_dash(x, 98, 10, PC_INK);
        } else {
            pc_box(x, 95 - h, 10, h, PC_INK);
            pc_hl(x, 97, 10, PC_DIM);
        }
    }
    str_cpy(b, "DEGREE ", sizeof b);
    fmt_int(b + 7, cur + 1u);
    pc_text(b, 1, 101, PC_DIM);
    str_cpy(b, "LEAP ", sizeof b);                      /* (KNOB 2: the tension) */
    fmt_int(b + 5, g->tension);
    pc_textr(b, 118, 101, PC_DIM);
}
static void pc_pic_morph(const gen_morph_t *g)
{
    uint32_t i, cur = g->step_idx & 15u;
    for (i = 0; i < 16u; i++) {
        int32_t x = 14 + (int32_t)i * 33 / 5;
        if ((g->snap_a >> i) & 1u) pc_fill(x, 55, 5, 9, PC_INK); else pc_fill(x, 62, 5, 2, PC_DIM);
        if ((g->snap_b >> i) & 1u) pc_fill(x, 73, 5, 9, PC_INK); else pc_fill(x, 80, 5, 2, PC_DIM);
        if (song.playing && i == cur) pc_hl(x, 84, 5, PC_INK);
    }
    pc_text("A", 1, 56, PC_DIM);
    pc_text("B", 1, 74, PC_DIM);
    pc_box(14, 92, 104, 6, PC_INK);                     /* where it sits between A and B */
    pc_fill(15, 93, (int32_t)g->morph_pos * 102 / 127, 4, PC_INK);
}
static void pc_pic_bounce(const gen_bounce_t *b)
{
    static const uint8_t LEAF[3][2] = {{1, 2}, {0, 4}, {1, 2}};   /* (a row's first cell and length) */
    uint32_t w = b->hit, n = b->balls >= 1u && b->balls <= GEN_BOUNCE_MAX ? b->balls : 1u, k;
    if (b->balls == GEN_BOUNCE_FALL) {
        pc_fill(2, w ? 105 : 106, 116, w ? 2 : 1, w ? PC_INK : PC_DIM);
        if (b->gust)
            for (k = 0; k < 4u; k++) {
                int32_t gx = 4 + (int32_t)b->lx[k * 2u] * 90 / 255 + (b->angle > 45u ? -4 : 4) * (int32_t)b->gust;
                pc_dash(gx < 2 ? 2 : gx, 56 + (int32_t)((b->ly[k * 2u] + 97u * k) & 255u) * 40 / 255, 12, PC_DIM);
            }
        for (k = 0; k < GEN_FALL_LEAVES; k++) {
            int32_t lx = 3 + (int32_t)b->lx[k] * 110 / 255, ly = 101 - (int32_t)b->ly[k] * 46 / 255, r;
            int tilt = (((int32_t)b->ly[k] + (int32_t)k * 40) & 63) < 32;
            if (b->lt[k]) {                              /* landed */
                if (b->lv[k] & 0x80u) pc_box(lx, 103, 4, 2, b->lt[k] > 3u ? PC_INK : PC_DIM);
                else pc_fill(lx, 103, 4, 2, b->lt[k] > 3u ? PC_INK : PC_DIM);
                continue;
            }
            for (r = 0; r < 3; r++)
                pc_hl(tilt ? lx + LEAF[r][0] + r - 1 : lx + 1 - r + LEAF[r][0], ly + r, LEAF[r][1], PC_INK);
        }
        return;
    }
    pc_fill(2, 53, 116, w & 8u ? 2 : 1, w & 8u ? PC_INK : PC_DIM);                      /* top */
    pc_fill(2, w & 1u ? 105 : 106, 116, w & 1u ? 2 : 1, w & 1u ? PC_INK : PC_DIM);      /* floor */
    pc_fill(2, 53, w & 2u ? 2 : 1, 54, w & 2u ? PC_INK : PC_DIM);                       /* left */
    pc_fill(w & 4u ? 116 : 117, 53, w & 4u ? 2 : 1, 54, w & 4u ? PC_INK : PC_DIM);      /* right */
    for (k = n; k-- > 0;) {
        uint32_t t = gen_bounce_touch(b, k);
        int32_t x = t & 2u ? 0 : t & 4u ? 255 : b->x[k], y = t & 1u ? 0 : t & 8u ? 255 : b->y[k];
        pc_disc(6 + x * 107 / 255, 102 - y * 45 / 255, 2, PC_INK);
    }
}
/* AWAKE: loop A (the notes) as bars 1..8 high over loop B (their lift) as its figure, 16 steps each; a rest a dash,
 * past a loop's length a dim line; the steps playing solid, the others outlined; the step the keys picked framed */
static void pc_pic_awake(const gen_awake_t *a)
{
    uint32_t la = a->len_a ? a->len_a : 1u, lb = a->len_b ? a->len_b : 1u, i;
    uint32_t ca = (a->pos_a + la - 1u) % la, cb = (a->pos_b + lb - 1u) % lb;
    char b[16];
    for (i = 0; i < 16u; i++) {
        int32_t x = 2 + (int32_t)i * 7 + (int32_t)(i / 4u);
        uint32_t va = gen_awake_a(a, i), vb = gen_awake_b(a, i);
        int on_a = song.playing && i == ca, on_b = song.playing && i == cb;
        if (i >= la) pc_hl(x, 80, 6, PC_DIM);
        else if (!va) pc_fill(x, 79, 6, 2, on_a ? PC_INK : PC_DIM);
        else {
            int32_t h = 3 + ((int32_t)va - 1) * 21 / 7;
            if (on_a) pc_fill(x, 81 - h, 6, h, PC_INK);
            else pc_box(x, 81 - h, 6, h, PC_INK);
        }
        if (i >= lb) pc_hl(x, 92, 6, PC_DIM);
        else {
            char d[2] = {(char)('0' + (vb > 7u ? 7u : vb)), 0};
            if (on_b) pc_inv(d, x + 1, 85);
            else pc_text(d, x + 1, 85, vb ? PC_INK : PC_DIM);
        }
        if (a->edit && (a->edit & 15u) == i)
            pc_box(x - 1, 54, 8, 40, PC_INK);
    }
    str_cpy(b, "A ", sizeof b);
    fmt_int(b + 2, la);
    str_cpy(b + str_len(b), "  +B ", 6);
    fmt_int(b + str_len(b), lb);
    pc_text(b, 1, 101, PC_DIM);
}
/* POLY: the four rhythms a row each: its divider (the one RHY picks inverted; OFF dim), the bar's 16 steps, a tick where
 * it lands (solid when it fires at the step playing); the step playing underlined; under them the four notes, a bar
 * each at its pitch, the one playing solid */
static void pc_pic_poly(const gen_poly_t *p)
{
    uint32_t n = 7u * (p->span >= 1u && p->span <= 3u ? p->span : 1u), now = (p->tick + GEN_POLY_WRAP - 1u) % GEN_POLY_WRAP;
    uint32_t base = now - now % 16u, r, i;
    for (r = 0; r < 4u; r++) {
        uint32_t d = p->div[r];
        int32_t y = 54 + (int32_t)r * 11;              /* (11 cells a row, the FM-1's 10: 8 px apart here) */
        char lb[4] = "OFF";
        if (d) lb[0] = '/', fmt_int(lb + 1, d);
        if (r == (p->sel & 3u))                  /* (the one RHY picks: a bar beside it, rows too close for a box) */
            pc_text(lb, 2, y, PC_INK), and_fill(1, pc_ty(y), 2, 7, PC_INK);
        else pc_text(lb, 2, y, d ? PC_INK : PC_DIM);
        for (i = 0; i < 16u; i++) {
            int32_t x = 22 + (int32_t)i * 6 + (int32_t)(i / 4u);
            uint32_t st = base + i, cur = song.playing && st == now;
            if (d && st % d == 0u) {
                if (cur) pc_fill(x, y, 5, 7, PC_INK);
                else pc_box(x, y, 5, 7, PC_INK);
            } else {
                pc_px(x + 2, y + 6, PC_DIM);
            }
            if (cur && r == 0u) pc_hl(x, 94, 5, PC_INK);
        }
    }
    for (i = 0; i < 4u; i++) {
        int32_t x = 22 + (int32_t)i * 25, h = 2 + (int32_t)(p->pitch[i] > n - 1u ? n - 1u : p->pitch[i]) * 9 / (int32_t)(n - 1u);
        if (i == p->pos) pc_fill(x, 107 - h, 22, h, PC_INK);
        else pc_box(x, 107 - h, 22, h, PC_INK);
    }
}
/* QUAD, drawn for this screen (not squeezed from the FM-1's): as norns' initenere draws its matrix, whose screen is
 * this one's size. The grid the tracks share, a row a track, 9 px apart: T1 (the track shown boxed) and its DIR, its
 * four cells as squares, a cell's brightness its pitch (a rest an outline; the cell it plays full; a row not in QUAD
 * dim), then its RANGE and its LINKs (a dot a track, lit when linked). Over each column its MOVE: a triangle the way it
 * goes, RAND two dots, STOP a dash (dim). A column's EVERY: not shown (the FM-1 has it under the grid; no room here) */
#define QP_X 32                                  /* the grid's left, a column's pitch, a cell's width */
#define QP_DX 13
#define QP_W 10
#define QP_Y (AND_PIC_Y + 5)                     /* the first row's top, a row's pitch, a cell's height */
#define QP_DY 9
#define QP_H 7
static void and_pic_quad(uint32_t tr)
{
    static const char *const QD[GEN_QD_COUNT] = {">", "<", "<>", "?", "="};
    static const char *const QR[GEN_QR_COUNT] = {"3", "1", "2", "+-1"};
    uint32_t r, i, k;
    for (i = 0; i < 4u; i++) {                   /* the columns' MOVE, in the 4 rows over the grid */
        int32_t cx = QP_X + (int32_t)i * QP_DX + QP_W / 2, y = AND_PIC_Y + 1;
        uint32_t mv = tessera.qcol[i] & 3u, c = mv ? PC_INK : PC_DIM;
        if (mv == GEN_QC_DOWN || mv == GEN_QC_UP)
            for (k = 0; k < 3u; k++)
                and_fill(cx - 2 + (int32_t)k, mv == GEN_QC_DOWN ? y + (int32_t)k : y + 2 - (int32_t)k,
                         5 - 2 * (int32_t)k, 1, c);
        else if (mv == GEN_QC_RAND)
            and_px(cx - 2, y, c), and_px(cx + 2, y + 2, c);
        else
            and_fill(cx - 2, y + 1, 5, 1, c);
    }
    for (r = 0; r < TESSERA_TRACKS; r++) {
        const gen_quad_t *q = tessera_quad(r);
        int on = tessera.mode[r] == TESS_ENG_QUAD;
        int32_t y = QP_Y + (int32_t)r * QP_DY;
        uint32_t c = on ? PC_INK : PC_DIM;
        char lb[3] = {'T', (char)('1' + r), 0};
        if (r == tr) {                           /* (the track shown: boxed, as the FM-1's) */
            and_fill(1, y - 1, 13, 9, PC_INK);
            and_text(2, y, lb, PC_BG, 2);
        } else
            and_text(2, y, lb, c, 2);
        and_text(17, y, QD[q->dir % GEN_QD_COUNT], c, 2);
        for (i = 0; i < 4u; i++) {
            int32_t x = QP_X + (int32_t)i * QP_DX;
            uint32_t v = q->cell[i] > 21u ? 21u : q->cell[i], cur = on && i == q->pos;
            if (!v)                              /* a rest: an outline */
                and_rect(x, y, QP_W, QP_H, cur ? PC_INK : on ? 4u : 2u);
            else if (cur)
                and_fill(x, y, QP_W, QP_H, PC_INK);
            else                                 /* (its pitch: 1..21 as levels 3..11; dim rows 1..3) */
                and_fill(x, y, QP_W, QP_H, on ? 3u + (v - 1u) * 8u / 20u : 1u + (v - 1u) * 2u / 20u);
        }
        and_text(QP_X + 4 * QP_DX + 3, y, QR[q->range % GEN_QR_COUNT], c, 3);
        for (k = 0; k < TESSERA_TRACKS; k++)     /* its LINKs, a dot a track (its own none) */
            if (k != r)
                and_fill(110 + (int32_t)k * 4, y + 3, 2, 2, on && ((q->link >> k) & 1u) ? PC_INK : 2u);
    }
}

/* CONSTELLATION: the sky scrolling left, the far two layers dim dots; the front layer's stars a plus their size (bright
 * ones and the ones heard ink, the others ahead ink and behind dim); behind the ship the stars it heard joined; the ship
 * mid-sky, its thruster flickering, the brackets SIZE rows round it (dim when the loop plays), the star sounding ringed.
 * LOCK up: the loop under the sky, its stars joined, the one played ringed, a mark where LEN ends it.
 * In a MOSAIC kind of palette the sky takes its four inks, as LANDSCAPE does: a star the ink of its size, the far
 * layers dim, the constellation's lines and the ship's brackets inks of their own, the ship the palette's white. A one-ink
 * palette: its ink and its dim, as before (its four inks are its ink, pc_tdim its dim) */
static void pc_pic_constel(uint32_t tr)
{
    const gen_constel_t *g = tessera_constel(tr);
    int lock = tessera.evolve[tr].lock != 0;
    int32_t top = 54, sh = lock ? 32 : 52, cw = 7, sx = 4 + 7 * cw + 3, row = g->ship >> 8;
    int32_t wd = g->size > 7u ? 7 : (int32_t)g->size, i, px = 0, py = 0, pv = 0;
#define MCS_Y(r) (top + sh - 2 - (int32_t)(r) * (sh - 4) / (int32_t)(GEN_CS_ROWS - 1u))
    for (uint32_t l = 2; l > 0; l--)
        for (i = -7; i < 9; i++) {
            uint32_t st = gen_cs_star(g->sky, ((uint32_t)g->col >> l) + (uint32_t)i, l, 30u);
            if (st) pc_px(sx + i * cw + (int32_t)l, MCS_Y((st & 15u) - 1u), pc_tdim(1u + l));
        }
    for (i = -7; i <= 0; i++) {                         /* (the constellation behind the ship: its lines first) */
        uint32_t st = gen_cs_star(g->sky, (uint32_t)g->col + (uint32_t)i, 0, g->dens);
        int32_t y;
        if (!st || !((g->trail >> (uint32_t)-i) & 1u)) continue;
        y = MCS_Y((st & 15u) - 1u);
        if (pv) pc_line(px, py, sx + i * cw, y, PC_TRK(2u));
        px = sx + i * cw, py = y, pv = 1;
    }
    for (i = -7; i < 9; i++) {
        uint32_t st = gen_cs_star(g->sky, (uint32_t)g->col + (uint32_t)i, 0, g->dens);
        int32_t x = sx + i * cw, y, a;
        int heard = i <= 0 && ((g->trail >> (uint32_t)-i) & 1u);
        uint32_t c;
        if (!st) continue;
        y = MCS_Y((st & 15u) - 1u);
        a = 1 + (int32_t)((st >> 6) & 3u) / 2;
        c = heard || (st >> 4) & 2u || i >= 0 ? PC_TRK((st >> 6) & 3u) : pc_tdim((st >> 6) & 3u);
        pc_hl(x - a, y, 2 * a + 1, c);
        pc_vl(x, y - a, 2 * a + 1, c);
        if (!i && g->caught) pc_circ(x, y, 4, PC_HOT);
    }
    {
        int32_t y = MCS_Y(row), yt = MCS_Y(row + wd > 14 ? 14 : row + wd) - 2, yb = MCS_Y(row - wd < 0 ? 0 : row - wd) + 2;
        uint32_t bc = g->held ? pc_tdim(3u) : PC_TRK(3u);
        pc_hl(sx - 2, yt, 5, bc); pc_px(sx - 2, yt + 1, bc); pc_px(sx + 2, yt + 1, bc);
        pc_hl(sx - 2, yb, 5, bc); pc_px(sx - 2, yb - 1, bc); pc_px(sx + 2, yb - 1, bc);
        pc_hl(sx - 11, y, 7, PC_HOT);                   /* the ship, a triangle nose right, and its thruster */
        pc_hl(sx - 11, y - 1, 5, PC_HOT); pc_hl(sx - 11, y + 1, 5, PC_HOT);
        pc_hl(sx - 11, y - 2, 3, PC_HOT); pc_hl(sx - 11, y + 2, 3, PC_HOT);
        pc_hl(sx - 13 - (int32_t)(g->col & 1u), y, 1 + (int32_t)(g->col & 1u), PC_DIM);
        if (g->filled && song.playing) {                /* (FILL ARP: a hollow diamond at the note it played) */
            int32_t fy = MCS_Y(g->filled - 1u);
            pc_px(sx, fy - 2, PC_INK); pc_px(sx, fy + 2, PC_INK); pc_px(sx - 2, fy, PC_INK); pc_px(sx + 2, fy, PC_INK);
            pc_px(sx - 1, fy - 1, PC_INK); pc_px(sx + 1, fy - 1, PC_INK); pc_px(sx - 1, fy + 1, PC_INK); pc_px(sx + 1, fy + 1, PC_INK);
        }
    }
#undef MCS_Y
    if (lock) {
        int32_t ly = 92, lh = 12;
        uint32_t now = song.playing && g->held ? (uint32_t)g->held - 1u : 16u, n = g->len >= 1u && g->len <= 16u ? g->len : 16u, k;
        int32_t qx = -1, qy = 0;
        pc_dash(4, 89, 112, PC_DIM);
        if (n < 16u) pc_vl(4 + (int32_t)n * cw, ly - 1, lh + 3, PC_INK);
        for (k = 0; k < n; k++) {
            uint32_t v = g->loop[k];
            int32_t x = 4 + (int32_t)k * cw + cw / 2, y;
            if (!(v & 15u)) {
                if (k == now) pc_fill(x - 1, ly + lh, 3, 2, PC_INK);
                continue;
            }
            y = ly + lh - (int32_t)((v & 15u) - 1u) * lh / (int32_t)(GEN_CS_ROWS - 1u);
            if (qx >= 0) pc_line(qx, qy, x, y, pc_tdim(2u));
            qx = x, qy = y;
            pc_fill(x - 1, y - 1, 3, 3, PC_TRK((v >> 6) & 3u));
            if (k == now) pc_circ(x, y, 3, PC_HOT);
        }
    }
}
/* SERIES: the next 16 degrees as bars (the one playing solid), a dim line joining their tops behind them; the loop's
 * length bracketed under them, LOOP OFF dashed (the series running on) */
static void pc_pic_series(const gen_series_t *g)
{
    gen_series_t p = *g;                                /* (from the step just played) */
    int32_t px = -1, py = 0;
    uint32_t k;
    if (p.n) p.n--;
    for (int pass = 0; pass < 2; pass++)
        for (k = 0; k < 16u; k++) {
            uint32_t d = gen_sr_degree(&p, k);
            int32_t x = 4 + (int32_t)k * 7, h = 2 + (int32_t)d * 38 / 14, y = 97 - h;
            if (!pass) {
                if (px >= 0) pc_line(px, py, x + 2, y, PC_DIM);
                px = x + 2, py = y;
            } else if (song.playing && g->n && k == 0u) pc_fill(x, y, 5, h, PC_INK);
            else pc_box(x, y, 5, h, PC_INK);
        }
    if (g->loop) {
        int32_t w = (int32_t)g->loop * 7 - 2;
        pc_hl(4, 103, w, PC_INK);
        pc_vl(4, 100, 4, PC_INK);
        pc_vl(4 + w - 1, 100, 4, PC_INK);
    } else
        pc_dash(4, 103, 110, PC_DIM);
}
/* a chord's name in a card x .. x + w - 1 from row y: on one line when it fits, else the root over its quality, "maj"
 * as "M" and "m7b5" as o slashed when the card is narrow; the small letters kept (Am7) */
static void pc_chord_card(char *b, int32_t x, int32_t w, int32_t y, uint32_t c)
{
    uint32_t rl = b[1] == '#' || b[1] == 'b' ? 2u : 1u, m = (uint32_t)(w - 2) / 6u;
    char r[3] = {b[0], rl > 1u ? b[1] : 0, 0}, *q = b + rl;
    if (str_len(b) <= m) {
        pc_textc(b, x + w / 2, y + 4, c);
    } else {
        if (str_len(q) > m && q[0] == 'm' && q[1] == 'a' && q[2] == 'j') q[2] = 'M', q += 2;
        if (str_len(q) > m && q[0] == 'm' && q[1] == '7' && q[2] == 'b' && q[3] == '5') q[0] = 'o', q[2] = 0;
        if (str_len(q) > m) q[m] = 0;
        pc_textc(r, x + w / 2, y, c);
        pc_textc(q, x + w / 2, y + 8, c);
    }
}
/* CHORD: the progression as cards (the one playing solid, a bar under it for how far through it is), then an octave
 * of keys from C, the playing chord's notes solid, its root with a notch */
static void pc_pic_chord(uint32_t tr)
{
    static const uint8_t BLACK[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
    const gen_chord_t *g = tessera_chord(tr);
    uint32_t n = gen_chord_len(g), cur = g->n ? g->idx : 0u, pcs = 0, len = gen_chord_steps(g, tess_steps_per_bar(&trk[tr]));
    uint32_t i, k;
    int32_t cw = 116 / (int32_t)(n ? n : 1u);
    char b[12];
    for (i = 0; i < n; i++) {
        int32_t x = 2 + (int32_t)i * cw;
        uint32_t at = tessera_chord_at(tr, i, b), on = i == cur;
        for (char *q = b; *q; q++)                      /* ("dim" as its symbol: B degree, C degree 7) */
            if (q[0] == 'd' && q[1] == 'i' && q[2] == 'm') {
                q[0] = 'o';                         /* (the FM-1's degree sign: not in and_text's face) */
                for (char *s2 = q + 1; (s2[0] = s2[2]) != 0; s2++) ;
                break;
            }
        if (on) pcs = at, pc_fill(x, 53, cw - 1, 18, PC_INK);
        else pc_box(x, 53, cw - 1, 18, PC_DIM);
        pc_chord_card(b, x, cw - 1, 55, on ? PC_BG : PC_INK);
        if (on && song.playing && g->n)
            pc_hl(x, 73, (cw - 1) * (int32_t)((g->n - 1u) % len + 1u) / (int32_t)len, PC_INK);
    }
    for (k = 0; k < 12u; k++) {
        int32_t x = 4 + (int32_t)k * 9, y = BLACK[k] ? 78 : 86;
        uint32_t lit = (pcs >> k) & 1u, root = k == (pcs >> 12);
        if (lit) pc_fill(x, y, 8, 107 - y - (BLACK[k] ? 8 : 0), PC_INK);
        else pc_box(x, y, 8, 107 - y - (BLACK[k] ? 8 : 0), PC_DIM);
        if (lit && root) pc_fill(x + 3, y + 2, 2, 3, PC_BG);
    }
}
/* ANSWER: the phrase on a staff of its steps, a note a square at its height, lines joining them, the one playing
 * ringed; under it what it does now: LISTENING, an answer of FADE's count (a square an answer, the played ones dim),
 * LOOPS or PLAY KEYS (a generator SOURCE: LISTENING). The squares outlined once the answers fade past half */
static void pc_pic_answer(uint32_t tr)
{
    const tessera_answer_t *a = tessera_answer(tr);
    uint32_t n = a->n, span = n ? (uint32_t)a->at[n - 1u] + 1u : 1u, lo = 127, hi = 0, ph = tess_an.phase[tr], i;
    uint32_t cnt = tessera_answer_count(tr), now = tess_an.slot[tr];
    int fade = ph == TESS_AN_PLAY && cnt && tess_an.ans[tr] * 2u >= cnt;
    int32_t px = -1, py = 0;
    char b[16];
    if (span < 8u) span = 8u;
    for (i = 0; i < n; i++) {
        lo = a->note[i] < lo ? a->note[i] : lo;
        hi = a->note[i] > hi ? a->note[i] : hi;
    }
    if (hi < lo + 7u) hi = lo + 7u;
    pc_hl(2, 97, 116, PC_DIM);
    for (int pass = 0; pass < 2; pass++)
        for (i = 0; i < n; i++) {
            int32_t x = 5 + (int32_t)a->at[i] * 110 / (int32_t)(span - 1u);
            int32_t y = 93 - (int32_t)(a->note[i] - lo) * 37 / (int32_t)(hi - lo);
            if (!pass) {
                if (px >= 0) pc_line(px, py, x, y, PC_DIM);
                px = x, py = y;
                continue;
            }
            if (fade && ph != TESS_AN_HEAR) pc_box(x - 1, y - 1, 3, 3, PC_INK);
            else pc_fill(x - 1, y - 1, 3, 3, PC_INK);
            if (song.playing && now == i + 1u && (ph == TESS_AN_PLAY || ph == TESS_AN_LOOP)) pc_circ(x, y, 3, PC_INK);
        }
    if (ph == TESS_AN_HEAR || (ph == TESS_AN_IDLE && answer_gen_src(tr))) str_cpy(b, "LISTENING", sizeof b);
    else if (!n || ph == TESS_AN_IDLE) str_cpy(b, "PLAY KEYS", sizeof b);
    else if (!cnt) str_cpy(b, "LOOPS", sizeof b);
    else {
        str_cpy(b, "ANSWER ", sizeof b);
        fmt_int(b + 7, (int32_t)(tess_an.ans[tr] + 1u));
        for (i = 0; i < cnt && i < 8u; i++) {
            int32_t x = 118 - (int32_t)(cnt - i) * 5;
            if (i == tess_an.ans[tr]) pc_fill(x, 102, 4, 4, PC_INK);
            else pc_box(x, 102, 4, 4, i < tess_an.ans[tr] ? PC_DIM : PC_INK);
        }
    }
    pc_text(b, 2, 101, ph == TESS_AN_HEAR ? PC_INK : PC_DIM);
}
/* STEP as seed: the loop as bars, a step the generator changed solid with the step as written outlined over it (cut
 * into the bar where it is lower), an unchanged one outlined, a rest a dash, past the loop a dot; the step playing
 * underlined. Under it MORPH's place between A and B (MORPH), and HOME: how many steps have moved off the written ones */
static void pc_pic_seed(uint32_t tr)
{
    uint8_t now[16], home[16];
    uint32_t at = 0, len = tessera_sd_loop(tr, now, home, &at), i, k, n = 0;
    int32_t lo = 127, hi = 0;
    for (i = 0; i < len; i++)
        for (k = 0; k < 2u; k++) {
            uint32_t e = k ? home[i] : now[i];
            if (e & 0x80u) {
                if ((int32_t)(e & 0x7Fu) < lo) lo = (int32_t)(e & 0x7Fu);
                if ((int32_t)(e & 0x7Fu) > hi) hi = (int32_t)(e & 0x7Fu);
            }
        }
    for (i = 0; i < 16u; i++) {
        int32_t x = 2 + (int32_t)i * 7 + (int32_t)(i / 4u), nh = 0;
        int head = song.playing && i == at, ch = i < len && now[i] != home[i];
        if (i >= len) {
            pc_px(x + 2, 92, PC_DIM);
            continue;
        }
        n += (uint32_t)ch;
        for (k = 0; k < 2u; k++) {
            uint32_t e = k ? home[i] : now[i];
            int32_t bh = 3 + (hi > lo ? ((int32_t)(e & 0x7Fu) - lo) * 33 / (hi - lo) : 0);
            if (k && !ch) break;
            if (!(e & 0x80u)) {
                if (!k) pc_hl(x, 92, 6, head ? PC_INK : PC_DIM);
                continue;
            }
            if (k) pc_box(x, 93 - bh, 6, bh, bh <= nh ? PC_BG : PC_INK);
            else if (ch || head) nh = bh, pc_fill(x, 93 - bh, 6, bh, PC_INK);
            else pc_box(x, 93 - bh, 6, bh, PC_INK);
        }
        pc_fill(x, 95, 6, head ? 2 : 1, head ? PC_INK : PC_DIM);
    }
    if (tessera.mode[tr] == TESS_ENG_MORPH) {
        pc_hl(2, 104, 50, PC_DIM);
        pc_fill(2 + (int32_t)tessera_morph(tr)->morph_pos * 47 / 127, 101, 3, 7, PC_INK);
    }
    pc_text("HOME", 60, 101, PC_DIM);
    pc_box(86, 101, 32, 7, PC_DIM);
    if (n) pc_fill(87, 102, (int32_t)(n * 30u / len) ? (int32_t)(n * 30u / len) : 1, 5, PC_INK);
}

/* ---- the pages' own: ENV (SOUND's AMP), LFO (MOD's first), PIECE (PIECE's first) ---- */

/* ENV: the envelope from its 4 settings (attack, decay, sustain, release, each 0..127 of its range): the curve, the
 * area under it hatched dim, a point at each corner. The FM-1's mo_pic_env (From Tessera 81aa730f) without the ring
 * on the setting turning (Andamento shows that setting large while it turns) */
static void pc_pic_env(int32_t va, int32_t vd, int32_t vs, int32_t vr)
{
    int32_t top = 56, bot = 98, x0 = 2, x1 = x0 + 5 + va * 20 / 127, x2 = x1 + 8 + vd * 26 / 127, x4 = 117;
    int32_t x3 = x4 - 12 - vr * 24 / 127, sus = vs * 1000 / 127, x, py = 0, e = 32768;
    int32_t kd = 75366 / (x2 - x1), kr = 75366 / (x4 - x3), i;
#define PCY(l) (bot - (l) * (bot - top) / 1000)
    for (x = x0; x <= x4; x++) {
        int32_t l, y, seg = x < x1 ? 0 : x < x2 ? 1 : x < x3 ? 2 : 3;
        if (seg == 0) l = (x - x0) * 1000 / (x1 - x0);
        else if (seg == 1) { e = x == x1 ? 32768 : (e * (32768 - kd)) >> 15; l = sus + ((1000 - sus) * e >> 15); }
        else if (seg == 2) l = sus;
        else { e = x == x3 ? 32768 : (e * (32768 - kr)) >> 15; l = x == x4 ? 0 : sus * e >> 15; }
        y = PCY(l);
        if (y < bot && !(x & 1)) pc_vl(x, y + 1, bot - y, PC_DIM);
        if (x > x0) pc_line(x - 1, py, x, y, PC_INK);
        py = y;
    }
    pc_hl(x0, bot, x4 - x0 + 1, PC_INK);
    {
        const int32_t hx[4] = {x1, x2, x3, x4}, hy[4] = {top, PCY(sus), PCY(sus), bot};
        for (i = 0; i < 4; i++)
            pc_disc(hx[i], hy[i], 1, PC_INK);
    }
#undef PCY
}

/* LFO: one cycle of the track's LFO 1 from its PHASE, a dot where it is now; FREE or SYNC and its rate at the foot
 * left, its value now at the right. The FM-1's mo_pic_lfo (From Tessera 81aa730f) */
static void pc_pic_lfo(track_t *t)
{
    int32_t x, py = 0, uni = t->p[P_LPOL] != 0, z = uni ? 98 : 80, amp = uni ? 40 : 18, now;
    uint32_t ph = (uint32_t)t->p[P_LPHASE] << 25, step = 0xFFFFFFFFu / 116u;
    char b[16];
    const char *unit;
    pc_dash(2, z, 116, PC_DIM);
    for (x = 0; x < 116; x++) {                  /* one cycle from PHASE, so the dot crosses it all; */
        int32_t w = lfo_wave(t, ph + (uint32_t)x * step), y;   /* S&H: this cycle's held value, flat */
        y = uni ? z - (w + 32768) * amp / 65536 : z - w * amp / 32768;
        if (x) pc_line(1 + x, py, 2 + x, y, PC_INK);
        py = y;
    }
    now = (int32_t)((t->lfo_ph - ph) / step);    /* where it is (lfo_val: UNI is already 0..+1) */
    if (now >= 0 && now < 116) {
        int32_t w = t->lfo_val;
        pc_vl(2 + now, PC_Y0 + 2, 50, PC_DIM);
        pc_disc(2 + now, z - w * amp / 32768, 2, PC_INK);
    }
    param_format(&TP[P_LSYNC], t->p[P_LSYNC], b + 5, &unit);
    if (t->p[P_LSYNC]) { b[0] = 'S'; b[1] = 'Y'; b[2] = 'N'; b[3] = 'C'; b[4] = ' '; pc_text(b, 2, 101, PC_DIM); }
    else pc_text("FREE", 2, 101, PC_DIM);   /* (101, not the FM-1's 102: rows squeezed, its foot inside) */
    {
        int32_t v = t->lfo_val * 100 / 32767, a = v < 0 ? -v : v;
        b[0] = v < 0 ? '-' : '+';
        b[1] = (char)('0' + a / 100);
        b[2] = '.';
        b[3] = (char)('0' + a / 10 % 10);
        b[4] = (char)('0' + a % 10);
        b[5] = 0;
        pc_textr(b, 118, 101, PC_INK);
    }
}

/* s seconds as M:SS or H:MM:SS (the FM-1's piece_time, ui_graph.c, which the board's build lacks) */
static void pc_piece_time(char *b, uint32_t s)
{
    char *o = b;
    if (s >= 3600u) {
        fmt_int(o, (int32_t)(s / 3600u));
        o += str_len(o);
        *o++ = ':';
        *o++ = (char)('0' + s % 3600u / 600u);
        *o++ = (char)('0' + s % 600u / 60u);
    } else {
        fmt_int(o, (int32_t)(s / 60u));
        o += str_len(o);
    }
    *o++ = ':';
    *o++ = (char)('0' + s % 60u / 10u);
    *o++ = (char)('0' + s % 10u);
    *o = 0;
}
/* PIECE: the planner's SHAPE over the piece's LENGTH (dim while FREE, no plan), USER's eight points, where it is now
 * while playing; at the foot how long it is, or what is left, or ENDED. The FM-1's mo_pic_piece (From Tessera
 * 81aa730f) without OCT-'s point picker, which is the FM-1's key grammar */
static void pc_pic_piece(void)
{
    uint32_t at = 0, left = 0, on = (uint32_t)tessera_piece_get(&at, &left), set = tessera.pl_length != 0u;
    int32_t i, py = 0;
    char b[20];
    pc_hl(2, 86, 116, PC_DIM);
    for (i = 0; i < 116; i++) {
        int32_t y = 86 - tessera_piece_shape((uint32_t)i * 65536u / 115u) * 32 / 32767;
        if (i) pc_line(1 + i, py, 2 + i, y, set ? PC_INK : PC_DIM);
        py = y;
    }
    if (tessera.pl_shape == PL_USER)
        for (i = 0; i < 8; i++)
            pc_disc(2 + i * 115 / 7, 86 - (int32_t)tessera.pl_user[i] * 32 / 100, 2, PC_INK);
    if (on && song.playing) pc_vl(2 + (int32_t)(at * 115u >> 16), 52, 36, PC_INK);
    if (!set) str_cpy(b, "FREE", sizeof b);
    else if (song.playing && !on) str_cpy(b, "ENDED", sizeof b);
    else {
        pc_piece_time(b, left);
        str_cpy(b + str_len(b), song.playing ? " LEFT" : " LONG", 6);
    }
    pc_text(b, 2, 92, set ? PC_INK : PC_DIM);
}

/* ---- which one ---- */
/* the generator picture of track tr (0..7), on GEN; 0 when it has none to show (tracks 5..8 with the generator OFF) */
static int and_pic_gen(uint32_t tr)
{
    if (tr >= NTRK) {                            /* tracks 5..8: Tessera's generators, run by and_ext.c */
        ae_track_t *x = &and_ext.t[(tr - NTRK) % AE_N];
        switch (x->gen) {
        case AE_TURING: pc_pic_turing(&x->turing); return 1;
        case AE_EUCLID: pc_pic_euclid(&x->euclid); return 1;
        case AE_MARKOV: pc_pic_markov(&x->markov); return 1;
        case AE_AWAKE: pc_pic_awake(&x->awake); return 1;
        case AE_POLY: pc_pic_poly(&x->poly); return 1;
        case AE_SERIES: pc_pic_series(&x->series); return 1;
        default: return 0;
        }
    }
    if (tr < TESSERA_TRACKS && tessera.mode[tr] != TESS_ENG_OFF) {
        uint8_t now[16], home[16];
        uint32_t at;
        if (tessera_sd_loop(tr, now, home, &at)) {   /* STEP as seed: its loop */
            pc_pic_seed(tr);
            return 1;
        }
        switch (tessera.mode[tr]) {
        case TESS_ENG_TURING: pc_pic_turing(tessera_turing(tr)); return 1;
        case TESS_ENG_EUCLID: pc_pic_euclid(tessera_euclid(tr)); return 1;
        case TESS_ENG_MARKOV: pc_pic_markov(tessera_markov(tr)); return 1;
        case TESS_ENG_MORPH: pc_pic_morph(tessera_morph(tr)); return 1;
        case TESS_ENG_GRIDS: pc_pic_grids(tessera_grids(tr), &trk[tr]); return 1;
        case TESS_ENG_BOUNCE: pc_pic_bounce(tessera_bounce(tr)); return 1;
        case TESS_ENG_AWAKE: pc_pic_awake(tessera_awake(tr)); return 1;
        case TESS_ENG_POLY: pc_pic_poly(tessera_poly(tr)); return 1;
        case TESS_ENG_QUAD: and_pic_quad(tr); return 1;
        case TESS_ENG_CONSTEL: pc_pic_constel(tr); return 1;
        case TESS_ENG_SERIES: pc_pic_series(tessera_series(tr)); return 1;
        case TESS_ENG_CHORD: pc_pic_chord(tr); return 1;
        case TESS_ENG_ANSWER: pc_pic_answer(tr); return 1;
        default: break;
        }
    }
    pc_pic_pattern(&trk[tr % NTRK]);             /* (no generator: the steps as written) */
    return 1;
}

/* the picture of page p's sub-page sub for track tr (0..3; tracks 5..8 have none of these pages): ENV on SOUND's AMP
 * when its first row is the envelope's 4 settings, LFO on MOD's first, PIECE on PIECE's first; 0 for the others
 * (and_ui.c draws its 8 tracks there) */
static int and_pic_page(uint32_t p, uint32_t sub, uint32_t tr)
{
    if (tr >= NTRK || sub >= and_nsubs)
        return 0;
    if (p == AB_SOUND && !strcmp(and_subs[sub].tab, "AMP")) {
        int32_t v[4];
        uint32_t k;
        for (k = 0; k < 4u; k++) {
            const param_desc_t *d = and_subs[sub].c[k] != PID_NONE ? and_desc(and_subs[sub].c[k], tr) : 0;
            if (!d || d->max <= d->min)
                return 0;                        /* (FM6: no shared envelope, its own on its operator pages) */
            v[k] = (clamp(and_get(and_subs[sub].c[k], tr), d->min, d->max) - d->min) * 127 / (d->max - d->min);
        }
        pc_pic_env(v[0], v[1], v[2], v[3]);
        return 1;
    }
    if (p == AB_MOD && sub == 0u) {
        pc_pic_lfo(&trk[tr]);
        return 1;
    }
    if (p == AB_PIECE && sub == 0u) {
        pc_pic_piece();
        return 1;
    }
    return 0;
}
