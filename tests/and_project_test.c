/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's project file, format 1 (platform/andamento/and_project.c), in the virtual prototype's own build
 * (web/andamento/sim.c, at 48 kHz).
 *   and_project_test [golden]           (run by tests/run_tests.sh; exit 1 on a failed check)
 *   and_project_test --fixture file     writes the golden file (tests/fixtures/andp_v1.and) from the busy project
 * Checks:
 *   round trip   a busy project (every saved setting of tracks 1..8 and of the song moved off its default, engines
 *                and presets changed, steps in patterns, the selected track, a name) saved, the music reset, loaded:
 *                every setting, step and pattern as it was; saved again, the same bytes
 *   skipped      a chunk, a pid and a track this build doesn't know: skipped, the rest loads
 *   defaults     what the file doesn't hold (a track, a pattern, the song's settings) at a new project's values
 *   clamped      a value outside its range: clamped
 *   refused      every cut-off length, a flipped byte, a bad CRC, a chunk past its parent, a bad name, a bad step:
 *                refused whole, nothing changed
 *   golden       tests/fixtures/andp_v1.and (format 1 as first written) still loads, with its settings and steps */
#include "../web/andamento/sim.c"

#include <stdio.h>

static uint32_t nfail;
#define CHECK(c, ...) do { if (!(c)) { nfail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint8_t f1[ANDP_MAX], f2[ANDP_MAX], f3[ANDP_MAX];

/* ------------------------------------------------------------- the music as a list --- */
/* every saved value (the song's, then tracks 1..8's, in the file's order), every pattern's bytes, the name */
#define SNAP_MAX 8192u
#define PAT_BYTES (4u * 2u + 1u + NSTEP * ANDP_STEP_BYTES)
typedef struct {
    uint32_t n;
    uint32_t key[SNAP_MAX];
    int32_t v[SNAP_MAX];
    uint8_t pat[NTRK][NPAT][PAT_BYTES];
    char name[ANDP_NAME_MAX + 1u];
} snap_t;
static snap_t s0, s1, s2;
#define SONG 15u                                 /* (the song's values: a "track" of their own in a key) */

static void snap_put(snap_t *s, uint32_t tr, uint32_t pid, int32_t v)
{
    if (s->n < SNAP_MAX) {
        s->key[s->n] = tr << 16 | pid;
        s->v[s->n++] = v;
    }
}
static void snap(snap_t *s)
{
    uint32_t i, t, k, b;
    memset(s, 0, sizeof *s);
    for (i = 0; i < pid_count(); i++)
        if (!(pid_at(i)->flags & PID_F_TRACK) && andp_saved(pid_at(i), 0))
            snap_put(s, SONG, pid_at(i)->pid, andp_core_get(pid_at(i)->pid, 0));
    for (k = 0; k < AM_NPRM; k++)
        snap_put(s, SONG, AM_PID + k, am_get(k));
    for (k = 0; k < AT_NPRM; k++)
        snap_put(s, SONG, AT_PID + k, at_get(k));
    snap_put(s, SONG, AS_PID + AS_SEL, (int32_t)AND_SEL);
    for (t = 0; t < AND_TRACKS; t++) {
        if (t < NTRK) {
            snap_put(s, t, AS_PID + AS_ENGINE, trk[t].eng_req);
            snap_put(s, t, AS_PID + AS_PRESET, trk[t].preset);
            snap_put(s, t, AS_PID + AS_PATTERN, trk[t].pattern);
            for (i = 0; i < pid_count(); i++)
                if ((pid_at(i)->flags & PID_F_TRACK) && andp_saved(pid_at(i), t))
                    snap_put(s, t, pid_at(i)->pid, andp_core_get(pid_at(i)->pid, t));
            for (b = 0; b < NPAT; b++) {
                const int16_t *tm;
                const step_t *st = andp_steps(t, b, &tm);
                uint8_t *o = s->pat[t][b];
                for (k = 0; k < 4u; k++)
                    o[2 * k] = (uint8_t)tm[k], o[2 * k + 1] = (uint8_t)((uint16_t)tm[k] >> 8);
                o[8] = pattern_dir[t][b];
                for (k = 0; k < NSTEP; k++)
                    CHECK(andp_step_pack(o + 9 + k * ANDP_STEP_BYTES, &st[k]), "track %u pattern %u step %u packs", t, b, k);
            }
        } else {
            for (k = 0; k < AE_NPRM; k++)
                snap_put(s, t, AE_PID + k, ae_get(k, t - NTRK));
            for (k = 0; k < 3u * (AE_GENS - 1u); k++)
                snap_put(s, t, AE_PID + AE_GPS + k, ae_get(AE_GPS + k, t - NTRK));
        }
    }
    memcpy(s->name, andp_name, sizeof s->name);
}
/* how many values differ, except the keys in skip (each printed, up to max) */
static uint32_t snap_diff(const snap_t *a, const snap_t *b, uint32_t max, const char *what, const uint32_t *skip,
                          uint32_t nskip)
{
    uint32_t i, j, d = 0, t, k;
    if (a->n != b->n) {
        printf("  %s: %u values against %u\n", what, a->n, b->n);
        return 1;
    }
    for (i = 0; i < a->n; i++) {
        int skipped = 0;
        for (j = 0; j < nskip; j++)
            skipped |= a->key[i] == skip[j];
        if (a->key[i] != b->key[i] || (a->v[i] != b->v[i] && !skipped)) {
            if (d++ < max)
                printf("  %s: track %u pid 0x%04X: %d against %d\n", what, a->key[i] >> 16, a->key[i] & 0xFFFFu,
                       a->v[i], b->v[i]);
        }
    }
    for (t = 0; t < NTRK; t++)
        for (k = 0; k < NPAT; k++)
            if (memcmp(a->pat[t][k], b->pat[t][k], PAT_BYTES)) {
                if (d++ < max)
                    printf("  %s: track %u pattern %u differs\n", what, t, k);
            }
    if (strcmp(a->name, b->name)) {
        d++;
        if (max)
            printf("  %s: name '%s' against '%s'\n", what, a->name, b->name);
    }
    return d;
}

/* ------------------------------------------------------------- a busy project --- */
static uint32_t rnd = 12345u;
static uint32_t rn(uint32_t n) { rnd = rnd * 1664525u + 1013904223u; return (rnd >> 8) % n; }
static int32_t pick(const param_desc_t *d)       /* a value in d's range, off its default where it can be */
{
    int32_t v = d->min;
    uint32_t tries;
    for (tries = 0; tries < 8u; tries++) {
        v = d->min + (int32_t)rn((uint32_t)(d->max - d->min + 1));
        if (v != d->def)
            break;
    }
    return v;
}
static void busy(void)
{
    uint32_t t, i, k, b;
    static const uint8_t ENG[NTRK] = {ENGI_FM6, ENGI_DRUM, ENGI_ORACLE, ENGI_FM6};
    for (t = 0; t < NTRK; t++) {
        set_engine_of(&trk[t], ENG[t]);
        if (ENGINES[ENG[t]]->npresets > 2u)
            apply_preset_to(&trk[t], 2u + t % (ENGINES[ENG[t]]->npresets - 2u));
        pid_set(PID_TESS(TX_MODE), t, 1u + t);       /* (a generator on each: TURING, EUCLID, MARKOV, MORPH) */
        for (i = 0; i < pid_count(); i++) {
            const pid_entry_t *e = pid_at(i);
            if ((e->flags & PID_F_TRACK) && andp_saved(e, t) && e->pid != PID_TESS(TX_MODE))
                pid_set(e->pid, t, pick(pid_desc(e->pid, t)));
        }
        for (b = 0; b < NPAT; b += 1u + t) {         /* some patterns, each with steps, its own timing and DIR */
            pattern_t *p = pattern_at(t, b);
            for (k = 0; k < NSTEP; k += 1u + rn(4)) {
                step_t *s = &p->step[k];
                s->time = rn(5) ? ST_NOTE : ST_TIE;
                s->n = (uint8_t)(1u + rn(4));
                for (i = 0; i < s->n; i++)
                    s->note[i] = (uint8_t)(36u + rn(48));
                s->vel = (uint8_t)(1u + rn(127));
                s->probability = (uint8_t)rn(101);
                s->flags = (uint8_t)rn(4);
            }
            for (i = 0; i < 4u; i++)
                p->timing[i] = (int16_t)pick(&TP[P_SLEN + i]);
            pattern_dir[t][b] = (uint8_t)rn(SEQ_DIRS);
        }
        pattern_apply(&trk[t], (t * 3u) % NPAT);      /* (each playing a different one, some empty) */
        for (i = 0; i < 4u; i++)                      /* (the one playing changed in place too) */
            trk[t].p[P_SLEN + i] = (int16_t)pick(&TP[P_SLEN + i]);
        memset(&trk[t].step[5], 0, sizeof trk[t].step[5]);
        trk[t].step[5].time = ST_NOTE, trk[t].step[5].n = 1, trk[t].step[5].note[0] = 60, trk[t].step[5].vel = 99;
    }
    for (i = 0; i < pid_count(); i++) {
        const pid_entry_t *e = pid_at(i);
        if (!(e->flags & PID_F_TRACK) && andp_saved(e, 0))
            pid_set(e->pid, 0, pick(pid_desc(e->pid, 0)));
    }
    for (k = 0; k < AM_NPRM; k++)
        am_set(k, pick(&AM_DESC[k]));
    for (k = 0; k < AT_NPRM; k++)
        at_set(k, pick(&AT_DESC[k]));
    for (t = 0; t < AE_N; t++) {
        for (k = 0; k < AE_NPRM; k++)
            if (k < AE_P1 || k > AE_P3)
                ae_set(k, t, pick(ae_desc(k, t)));
        ae_set(AE_GEN, t, 1u + t);
        for (k = 0; k < 3u * (AE_GENS - 1u); k++)
            ae_set(AE_GPS + k, t, pick(ae_desc(AE_GPS + k, t)));
    }
    and_select(6);
    andp_set_name("STILL TIDE 2");
}

/* ------------------------------------------------------------- hand-made files --- */
static void w_name(andp_w_t *w, const char *s)
{
    uint32_t c = andp_begin(w, ANDP_ID('N', 'A', 'M', 'E')), i;
    for (i = 0; s[i]; i++)
        andp_byte(w, (uint8_t)s[i]);
    andp_end(w, c);
}

static void test_round_trip(void)
{
    uint32_t n1, n2, d;
    andp_defaults();
    busy();
    snap(&s1);
    n1 = andp_save(f1, sizeof f1);
    CHECK(n1 > 0, "a busy project saves");
    CHECK(andp_check(f1, n1) == ANDP_OK, "the saved file checks out");
    printf("  a busy project: %u bytes, %u values\n", n1, s1.n);
    andp_defaults();
    snap(&s0);
    CHECK(snap_diff(&s1, &s0, 0, "", 0, 0) > 100u, "the reset changed the music (else the round trip proves nothing)");
    CHECK(andp_load(f1, n1) == ANDP_OK, "the busy project loads");
    snap(&s2);
    d = snap_diff(&s1, &s2, 12, "round trip", 0, 0);
    CHECK(d == 0, "after the round trip every value, step and pattern as saved (%u differ)", d);
    n2 = andp_save(f2, sizeof f2);
    CHECK(n2 == n1 && !memcmp(f1, f2, n1), "saved again, the same bytes (%u against %u)", n2, n1);
    CHECK(andp_save(f2, n1 - 1u) == 0, "a buffer one byte short: refused, not cut");
}

static void test_skipped_defaults_clamped(void)
{
    andp_w_t w;
    uint32_t c, tc, n, d, bpm = PID_GLOBAL(G_BPM);
    const param_desc_t *bd = pid_desc(bpm, 0);
    const uint32_t changed[3] = {SONG << 16 | bpm, SONG << 16 | (AM_PID + AM_MIX), 1u << 16 | P_LEVEL};
    andp_defaults();
    snap(&s0);                                       /* (a new project's) */
    busy();                                          /* (the music before: the load must reset it) */
    andp_start(&w, f3, sizeof f3);
    c = andp_begin(&w, ANDP_ID('Z', 'Z', 'Z', 'Z'));  /* an unknown chunk, odd length (a pad byte) */
    andp_byte(&w, 1), andp_byte(&w, 2), andp_byte(&w, 3);
    andp_end(&w, c);
    c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));
    andp_pair(&w, 0xEFFFu, 5);                      /* unknown pids */
    andp_pair(&w, 0x01FEu, 5);
    andp_pair(&w, bpm, 9999);                       /* out of range: clamped */
    andp_pair(&w, AM_PID + AM_MIX, -50);
    andp_end(&w, c);
    tc = andp_begin(&w, ANDP_ID('T', 'R', 'A', 'K')); /* a track this build doesn't have */
    andp_byte(&w, 12);
    c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));
    andp_pair(&w, AE_PID + AE_CHAN, 3);
    andp_end(&w, c);
    andp_end(&w, tc);
    tc = andp_begin(&w, ANDP_ID('T', 'R', 'A', 'K')); /* track 2: one setting, an unknown sub-chunk, a pattern of a
                                                       * newer step format and one past NPAT (both skipped) */
    andp_byte(&w, 1);
    c = andp_begin(&w, ANDP_ID('X', 'T', 'R', 'A'));
    andp_w32(&w, 7);
    andp_end(&w, c);
    c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));
    andp_pair(&w, P_LEVEL, 3);
    andp_end(&w, c);
    c = andp_begin(&w, ANDP_ID('P', 'A', 'T', 'N'));
    andp_byte(&w, 0), andp_byte(&w, 9);
    for (n = 0; n < 10u; n++)
        andp_byte(&w, 0);
    andp_end(&w, c);
    c = andp_begin(&w, ANDP_ID('P', 'A', 'T', 'N'));
    andp_byte(&w, NPAT), andp_byte(&w, ANDP_STEP_FMT);
    for (n = 0; n < 10u; n++)
        andp_byte(&w, 0);
    andp_end(&w, c);
    andp_end(&w, tc);
    w_name(&w, "NEW 1");                             /* (the name last: chunks in any order) */
    n = andp_finish(&w);
    CHECK(n && andp_load(f3, n) == ANDP_OK, "a file with unknown chunks, pids and tracks loads");
    CHECK(pid_get(bpm, 0) == bd->max, "a tempo of 9999 clamped to %d (%d)", bd->max, pid_get(bpm, 0));
    CHECK(am_get(AM_MIX) == AM_DESC[AM_MIX].min, "master MIX -50 clamped to its least");
    CHECK(motion_base_value(&trk[1], P_LEVEL) == 3, "track 2's LEVEL from the file");
    CHECK(!strcmp(andp_name, "NEW 1"), "the name from the file");
    snap(&s2);
    strcpy(s0.name, "NEW 1");
    d = snap_diff(&s0, &s2, 8, "defaults", changed, 3);
    CHECK(d == 0, "what the file doesn't hold at a new project's values, every pattern too (%u differ)", d);
}

static void test_refused(void)
{
    uint32_t n, cut, i, bad = 0, d;
    andp_w_t w;
    andp_defaults();
    busy();
    n = andp_save(f1, sizeof f1);
    andp_defaults();
    rnd ^= 0x5555u;
    busy();                                          /* (another busy project: the one a refusal must leave) */
    and_select(2);
    snap(&s1);
    for (cut = 0; cut < n; cut++)                    /* every cut-off length */
        if (andp_load(f1, cut) == ANDP_OK)
            bad++;
    CHECK(bad == 0, "every cut-off length refused (%u loaded)", bad);
    for (i = 12u, bad = 0; i < n - 4u; i += 97u) {   /* a flipped byte (the CRC catches it) */
        f1[i] ^= 0x20u;
        if (andp_load(f1, n) == ANDP_OK)
            bad++;
        f1[i] ^= 0x20u;
    }
    CHECK(bad == 0, "a flipped byte refused (%u loaded)", bad);
    f1[n - 1u] ^= 1u;
    CHECK(andp_load(f1, n) == ANDP_E_CRC, "a bad CRC refused");
    f1[n - 1u] ^= 1u;
    f1[0] = 'X';
    CHECK(andp_load(f1, n) == ANDP_E_MAGIC, "not a project refused");
    f1[0] = 'A';
    f1[8] = 2;
    CHECK(andp_load(f1, n) == ANDP_E_FORMAT, "a container format 2 refused");
    f1[8] = 1;
    f1[n] = 0;
    CHECK(andp_load(f1, n + 1u) != ANDP_OK, "a byte after the end refused");

    andp_start(&w, f3, sizeof f3);                   /* a chunk running past its parent (the CRC right) */
    {
        uint32_t tc = andp_begin(&w, ANDP_ID('T', 'R', 'A', 'K')), c;
        andp_byte(&w, 0);
        c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));
        andp_pair(&w, P_LEVEL, 3);
        andp_end(&w, c);
        f3[c + 4] = 40;                              /* (its length past the TRAK's end) */
        andp_end(&w, tc);
    }
    n = andp_finish(&w);
    CHECK(andp_load(f3, n) == ANDP_E_CHUNK, "a chunk past its parent refused");
    andp_start(&w, f3, sizeof f3);
    w_name(&w, "lower case");
    n = andp_finish(&w);
    CHECK(andp_load(f3, n) == ANDP_E_DATA, "a name in lower case refused");
    andp_start(&w, f3, sizeof f3);
    w_name(&w, "TWENTY ONE CHARACTERS");
    n = andp_finish(&w);
    CHECK(andp_load(f3, n) == ANDP_E_DATA, "a name of 21 characters refused");
    andp_start(&w, f3, sizeof f3);                   /* a step that can't be (a kind of 7, a chance past 101) */
    {
        uint32_t tc = andp_begin(&w, ANDP_ID('T', 'R', 'A', 'K')), c, k;
        andp_byte(&w, 0);
        c = andp_begin(&w, ANDP_ID('P', 'A', 'T', 'N'));
        andp_byte(&w, 0), andp_byte(&w, ANDP_STEP_FMT);
        for (k = 0; k < 9u; k++)
            andp_byte(&w, 0);
        andp_byte(&w, 1);
        for (k = 0; k < ANDP_STEP_BYTES; k++)
            andp_byte(&w, 0xFFu);
        andp_end(&w, c);
        andp_end(&w, tc);
    }
    n = andp_finish(&w);
    CHECK(andp_load(f3, n) == ANDP_E_DATA, "a bad step refused");
    snap(&s2);
    d = snap_diff(&s1, &s2, 8, "refused", 0, 0);
    CHECK(d == 0, "after the refusals the music as it was (%u differ)", d);

    andp_start(&w, f3, sizeof f3);
    w_name(&w, "TWENTY CHARACTERS 20");
    n = andp_finish(&w);
    CHECK(andp_load(f3, n) == ANDP_OK && !strcmp(andp_name, "TWENTY CHARACTERS 20"), "a name of 20 characters loads");
    CHECK(!andp_set_name("ab") && !andp_set_name("TWENTY ONE CHARACTERS") && andp_set_name("A1 B2"),
          "andp_set_name takes upper case, digits and spaces, up to 20");
}

/* the golden file: format 1 as first written. A pid changing meaning, or the steps' packing, shows here */
static void test_golden(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t n;
    if (!f) {
        CHECK(0, "no golden file %s", path);
        return;
    }
    n = (uint32_t)fread(f3, 1, sizeof f3, f);
    fclose(f);
    CHECK(andp_load(f3, n) == ANDP_OK, "the golden file (%u bytes) loads", n);
    CHECK(!strcmp(andp_name, "STILL TIDE 2"), "its name");
    CHECK(trk[0].eng_req == ENGI_FM6 && trk[1].eng_req == ENGI_DRUM && trk[2].eng_req == ENGI_ORACLE,
          "its engines (FM6, DRUM, ORACLE)");
    CHECK(pid_get(PID_TESS(TX_MODE), 1) == 2, "track 2's generator (EUCLID)");
    CHECK(trk[0].step[5].time == ST_NOTE && trk[0].step[5].note[0] == 60 && trk[0].step[5].vel == 99,
          "track 1's step 6 (C4, velocity 99)");
    CHECK(trk[1].pattern == 3u, "track 2 playing pattern 4");
    CHECK(AND_SEL == 6u, "track 7 selected");
    CHECK(ae_get(AE_GEN, 2) == 3, "track 7's generator (MARKOV)");
}

int main(int argc, char **argv)
{
    web_boot();
    if (argc > 2 && !strcmp(argv[1], "--fixture")) {
        FILE *f;
        uint32_t n;
        andp_defaults();
        busy();
        n = andp_save(f1, sizeof f1);
        if (!n || !(f = fopen(argv[2], "wb")))
            return 1;
        fwrite(f1, 1, n, f);
        fclose(f);
        printf("wrote %s (%u bytes)\n", argv[2], n);
        return 0;
    }
    test_round_trip();
    test_skipped_defaults_clamped();
    test_refused();
    test_golden(argc > 1 ? argv[1] : "tests/fixtures/andp_v1.and");
    printf(nfail ? "and_project: %u failed\n" : "and_project: all checks passed\n", nfail);
    return nfail ? 1 : 0;
}
