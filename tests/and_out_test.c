/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's 8 tracks and their outputs (platform/andamento/and_out.c) on the PC, through the virtual prototype's own
 * build (web/andamento/sim.c, at 48 kHz) driven as the page drives it, the MIDI read as the page reads it.
 *   and_out_test [dir]   (run by tests/and_tests.sh; exit 1 on a failed check; with a dir, frames as dir/ppm/out_*.ppm)
 * Checks:
 *   defaults  tracks 5..8 TURING, EUCLID, MARKOV, AWAKE, SOUND OFF, MIDI channels 5..8, CV pairs 1..4; tracks 1..4
 *             the synth, MIDI OFF, no CV; stopped, nothing is sent
 *   play      a demo playing: tracks 5..8 send notes on their own channels, each note-on ended by its note-off before
 *             the next (a mono line), in track 1's scale; nothing on 1..4; no voice of tracks 5..8 sounds (SOUND OFF);
 *             the CV is the note (1 V an octave, 0 V at C2) and the gate high while it sounds
 *   STOP      every note off, every gate low, nothing more sent; PLAY again: tracks 6..8 the same notes from the top
 *   MUTE, MIDI OFF  a muted track sends nothing; MIDI OFF sends no MIDI but still drives its CV / gate
 *   keys      track 5 selected (FUNC + key 5): a key sends its note at once on channel 5, its release the note-off
 *   pages     MIX's OUT sub-page: SOUND, MIDI CH, CV; CV moves a track to another pair; SOUND ON gives track 5 voices;
 *             LEFT / RIGHT move through all 8 tracks
 *   core      tracks 1..4 play exactly the same with tracks 5..8 playing as with them muted (bit for bit) */
#define AND_LINT 1                               /* and_fb.c keeps the text drawn (and_lint.h) */
#include "../web/andamento/sim.c"

#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static uint32_t nfail;
#define CHECK(c, ...) do { if (!(c)) { nfail++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
#include "and_lint.h"

static const char *dir;
static void btn(uint32_t b) { web_button(b, 1); web_button(b, 0); }
static void func_key(uint32_t k) { web_button(AB_FUNC, 1); web_key(k, 1); web_key(k, 0); web_button(AB_FUNC, 0); }

/* the MIDI sent, per channel: note-ons, note-offs, the note held, faults */
static struct {
    uint32_t on[16], off[16], bad_off[16], double_on[16], out_of_scale;
    int held[16];
    uint32_t first[16][32], nfirst[16];          /* the first notes on each channel (PLAY again replays them) */
} md;
static void midi_clear(void)
{
    uint32_t c;
    memset(&md, 0, sizeof md);
    for (c = 0; c < 16u; c++)
        md.held[c] = -1;
}
static void midi_read(void)
{
    uint32_t n = web_midi_take(), i;
    const uint8_t *m = web_midi_buf();
    for (i = 0; i < n; i++, m += 3) {
        uint32_t c = m[0] & 15u;
        if ((m[0] & 0xF0u) == 0x90u && m[2]) {
            if (md.held[c] >= 0)
                md.double_on[c]++;
            md.held[c] = m[1];
            if (md.nfirst[c] < 32u)
                md.first[c][md.nfirst[c]++] = m[1];
            if (!((scale_mask(&trk[0]) >> ((m[1] + 120u - (uint32_t)trk[0].p[P_ROOT] % 12u) % 12u)) & 1u))
                md.out_of_scale++;
            md.on[c]++;
        } else if ((m[0] & 0xF0u) == 0x80u || (m[0] & 0xF0u) == 0x90u) {
            if (md.held[c] != (int)m[1])
                md.bad_off[c]++;
            md.held[c] = -1;
            md.off[c]++;
        }
    }
}
static int32_t note_mv(uint32_t note)
{
    int32_t mv = ((int32_t)note - 36) * 1000 / 12;
    return mv < 0 ? 0 : mv > AO_CV_MAX ? AO_CV_MAX : mv;
}
static int sounding_any(uint32_t tr)
{
    uint32_t v;
    for (v = 0; v < NVOICE; v++)
        if (trk[tr].v[v].active)
            return 1;
    return 0;
}
/* ms of device time, the MIDI read every millisecond (the page reads it every 2.7 ms) */
static uint32_t gate_bad, voice_bad;
static void run_ms(uint32_t ms)
{
    while (ms--) {
        uint32_t p, tr;
        web_ms();
        midi_read();
        for (p = 0; p < AO_PAIRS; p++) {          /* the CV is the note sounding, the gate high while it sounds */
            if (and_out.pair[p].gate && and_out.pair[p].mv != note_mv(and_out.pair[p].note))
                gate_bad++;
            if ((int)web_gate(p) != and_out.pair[p].gate || web_cv(p) != and_out.pair[p].mv)
                gate_bad++;
        }
        for (tr = 4; tr < NTRK; tr++)             /* (SOUND OFF: no voice) */
            if (pid_get(AO_PID_SOUND, tr) && sounding_any(tr))
                voice_bad++;
    }
}
static void dump(const char *name)
{
    char p[256];
    FILE *f;
    const uint8_t *fb = web_screen();
    uint32_t i;
    and_lint_text(name);                         /* (the text of the draw just made: and_lint.h) */
    if (!dir)
        return;
    snprintf(p, sizeof p, "%s/ppm/%s.ppm", dir, name);
    if (!(f = fopen(p, "wb")))
        return;
    fprintf(f, "P6\n%d %d\n255\n", AND_W, AND_H);
    for (i = 0; i < AND_W * AND_H; i++) {
        uint32_t l = fb[i] & 15u;
        fputc((int)(255u * l / 15u), f);
        fputc((int)(208u * l / 15u), f);
        fputc((int)(32u * l / 15u), f);
    }
    fclose(f);
}

/* a hash of tracks 1..4's notes (the note feed) and of their audio over ms of the demo from its start, in a copy of
 * this process (fork) so both runs begin from the very same state; out 0: tracks 5..8 muted first */
static uint32_t core_run(uint32_t ms, int out)
{
    int fd[2];
    uint32_t h = 2166136261u;
    pid_t child;
    if (pipe(fd) || (child = fork()) < 0)
        return 0;
    if (!child) {
        uint8_t seen = land_ev_at;
        uint32_t x;
        close(fd[0]);
        land_open = 1;
        web_demo(0);
        if (!out)
            for (x = 4; x < NTRK; x++)
                pid_set(PID_TRACK(P_MUTE), x, 1);
        while (ms--) {
            uint32_t k;
            web_render(48);
            for (k = 0; k < 48u; k++) {
                union { float f; uint32_t u; } l = {out_l[k]}, r = {out_r[k]};
                h = (h ^ l.u) * 16777619u;
                h = (h ^ r.u) * 16777619u;
            }
            (void)web_midi_take();
            for (; seen != land_ev_at; seen++)
                if (land_ev_tr[seen & (LAND_EV - 1u)] < 4u)
                    h = (h ^ (land_ev_tr[seen & (LAND_EV - 1u)] << 8 | land_ev_note[seen & (LAND_EV - 1u)])) * 16777619u;
        }
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(child, 0, 0);
    return h;
}

int main(int argc, char **argv)
{
    static const uint8_t GEN[4] = {TESS_ENG_TURING, TESS_ENG_EUCLID, TESS_ENG_MARKOV, TESS_ENG_AWAKE};
    uint32_t x, c, i;
    dir = argc > 1 ? argv[1] : 0;
    web_boot();
    midi_clear();
    run_ms(200);

    /* defaults; stopped, silent */
    CHECK(NTRK == 8u, "8 tracks in the core (%u)", (uint32_t)NTRK);
    for (x = 0; x < NTRK; x++) {
        int out = x >= 4u;
        CHECK(pid_get(AO_PID_SOUND, x) == out, "track %u: SOUND %s", x + 1u, out ? "OFF" : "ON");
        CHECK(pid_get(AO_PID_MIDI, x) == (out ? (int32_t)x + 1 : TESS_MIDI_OFF), "track %u: MIDI CH (%d)", x + 1u,
              pid_get(AO_PID_MIDI, x));
        CHECK(and_out_pair_of(x) == (out ? (int32_t)x - 4 : -1), "track %u: CV pair (%d)", x + 1u, and_out_pair_of(x));
        if (out)
            CHECK(pid_get(PID_TESS(TX_MODE), x) == GEN[x - 4u], "track %u: its generator (%d)", x + 1u,
                  pid_get(PID_TESS(TX_MODE), x));
    }
    for (c = 0; c < 16u; c++)
        CHECK(!md.on[c], "stopped: nothing sent on channel %u", c + 1u);

    /* playing: the demo */
    web_demo(0);
    run_ms(8000);
    CHECK(song.playing, "the demo plays");
    for (x = 4; x < NTRK; x++) {
        c = x;
        CHECK(md.on[c] >= 4u, "track %u: notes on channel %u (%u)", x + 1u, c + 1u, md.on[c]);
        CHECK(!md.double_on[c] && !md.bad_off[c], "track %u: one note at a time, each ended (%u, %u)", x + 1u,
              md.double_on[c], md.bad_off[c]);
    }
    printf("notes in 8 s on channels 5..8: %u %u %u %u\n", md.on[4], md.on[5], md.on[6], md.on[7]);
    CHECK(!md.out_of_scale, "every note in track 1's scale (%u outside)", md.out_of_scale);
    CHECK(!gate_bad, "the CV the note, the gate high while it sounds (%u wrong)", gate_bad);
    CHECK(!voice_bad, "SOUND OFF: no voice of tracks 5..8 sounds (%u)", voice_bad);
    for (c = 0; c < 16u; c++)
        if (c < 4u || c > 7u)
            CHECK(!md.on[c], "nothing on channel %u (%u)", c + 1u, md.on[c]);
    and_ui.page = AND_HOME;
    dump("out_home");

    /* STOP; PLAY again replays from the SEED */
    {
        uint32_t first[4][8];
        for (x = 0; x < 4u; x++)
            memcpy(first[x], md.first[4u + x], sizeof first[x]);
        btn(AB_STOP);
        run_ms(100);
        for (x = 0; x < 4u; x++)
            CHECK(md.held[4u + x] < 0 && !web_gate(x), "STOP: track %u's note off, gate low", x + 5u);
        midi_clear();
        run_ms(1000);
        CHECK(!md.on[4] && !md.on[5] && !md.on[6] && !md.on[7], "stopped: nothing sent");
        btn(AB_PLAY);
        run_ms(3000);
        for (x = 1; x < 4u; x++)                 /* (not TURING: the core's keeps its loop where it was) */
            CHECK(md.nfirst[4u + x] >= 3u && !memcmp(first[x], md.first[4u + x], 3u * sizeof(uint32_t)),
                  "PLAY again: track %u's first notes the same", x + 5u);
    }

    /* MUTE, MIDI OFF */
    pid_set(PID_TRACK(P_MUTE), 4, 1);
    pid_set(AO_PID_MIDI, 5, TESS_MIDI_OFF);
    run_ms(50);
    midi_clear();
    gate_bad = 0;
    {
        uint32_t gates = 0, gates0 = 0;
        for (i = 0; i < 4000u; i++) {
            run_ms(1);
            gates += web_gate(1);
            gates0 += web_gate(0);
        }
        CHECK(!md.on[4] && !gates0, "MUTE: track 5 sends nothing (%u notes, %u ms of gate)", md.on[4], gates0);
        CHECK(!md.on[5] && gates > 0u, "MIDI OFF: track 6 no MIDI (%u), its gate still played (%u ms)", md.on[5], gates);
        CHECK(md.on[6] > 0u, "track 7 plays on");
    }
    pid_set(PID_TRACK(P_MUTE), 4, 0);
    pid_set(AO_PID_MIDI, 5, 6);

    /* a key on track 5 */
    btn(AB_STOP);
    run_ms(100);
    func_key(4);
    CHECK(AND_SEL == 4u, "FUNC + key 5 selects track 5 (%u)", AND_SEL);
    func_key(8);                                     /* (SCALE) */
    midi_clear();
    web_key(0, 1);
    run_ms(2);
    CHECK(md.on[4] == 1u && md.held[4] == (int)ak_key_note(4, 0, AK_SCALE), "a key: its note on channel 5 at once (%d)",
          md.held[4]);
    CHECK(web_gate(0) && web_cv(0) == note_mv((uint32_t)md.held[4]), "a key: gate high, CV its note");
    CHECK(!sounding_any(4), "a key on track 5 (SOUND OFF): no voice of its own");
    web_key(0, 0);
    run_ms(2);
    CHECK(md.off[4] == 1u && md.held[4] < 0 && !web_gate(0), "the key let go: its note-off, gate low");

    /* MIX's OUT: SOUND, MIDI CH, CV */
    btn(AB_MIX);
    if (and_ui.sub[AB_MIX] != AND_MIX_OUT)
        btn(AB_MIX);
    and_cells();
    CHECK(and_ui.sub[AB_MIX] == AND_MIX_OUT && and_ui.cell[0] == AO_PID_SOUND && and_ui.cell[1] == AO_PID_MIDI,
          "MIX's OUT: SOUND, MIDI CH");
    for (i = 0; i < 8u && and_ui.cell[i] != AO_PID + AO_CV; i++)
        ;
    CHECK(i < 8u, "MIX's OUT: CV");
    dump("out_mix");
    if (i < 8u) {
        web_enc(i, 1);                                /* (CV 1 -> 2: track 5 on pair 2, with track 6) */
        CHECK(and_out_pair_of(4) == 1, "a turn of CV: track 5 on pair 2 (%d)", and_out_pair_of(4));
        web_enc(i, -1);
        CHECK(and_out_pair_of(4) == 0, "back on pair 1 (%d)", and_out_pair_of(4));
    }
    web_enc(0, -1);                                   /* (SOUND OFF -> ON) */
    CHECK(pid_get(AO_PID_SOUND, 4) == 0, "encoder 1 on OUT: track 5's SOUND ON (%d)", pid_get(AO_PID_SOUND, 4));
    web_key(0, 1);
    run_ms(5);
    CHECK(sounding_any(4) && web_gate(0), "SOUND ON: a key on track 5 sounds a voice and still drives its gate");
    web_key(0, 0);
    run_ms(300);
    web_enc(0, 1);
    CHECK(pid_get(AO_PID_SOUND, 4) == 1, "SOUND OFF again");
    for (i = 0; i < 8u; i++)
        btn(AB_RIGHT);
    CHECK(AND_SEL == 4u, "RIGHT 8 times: round the 8 tracks back to 5 (%u)", AND_SEL);
    for (i = 0; i < 4u; i++)
        btn(AB_RIGHT);
    CHECK(AND_SEL == 0u, "RIGHT from 8: track 1 (%u)", AND_SEL);
    btn(AB_NO);

    /* tracks 1..4 the same with tracks 5..8 playing as with them muted */
    {
        uint32_t h1 = core_run(8000, 1), h2 = core_run(8000, 0), h3 = core_run(8000, 1);
        CHECK(h1 && h1 == h3, "the demo's run repeats bit for bit (%08x %08x)", h1, h3);
        CHECK(h1 == h2, "tracks 1..4: the same notes and audio with or without 5..8 (%08x %08x)", h1, h2);
    }

    printf(nfail ? "and_out: %u failed\n" : "and_out: all checks passed\n", nfail);
    return nfail ? 1 : 0;
}
