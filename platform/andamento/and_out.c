/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's outputs besides the audio: every track's own choice of where its notes go (FORK_PLAN step 5, 2026-11-16,
 * when tracks 5..8 became the core's own: NTRK 8). Three switches a track, all on MIX's OUT sub-page:
 *
 *   SOUND    (Tessera's TX_SOUND)   ON: the track's engine plays its notes; OFF: no voice (none of the voice budget)
 *   MIDI CH  (Tessera's TX_MIDI_CH) the channel its notes go out on, AUTO (ROUT's: track n on channel n), 1..16, OFF
 *   CV       (AO_PID + AO_CV)        OFF, or a pair of the jack strip, 1..4: its last note as 1 V an octave, its gate
 *
 * The defaults keep what Andamento had before (tracks 5..8 were and_ext.c's external tracks then): tracks 1..4 the
 * synth, no MIDI, no CV; tracks 5..8 a generator each (TURING, EUCLID, MARKOV, AWAKE) with SOUND OFF, MIDI channels
 * 5..8 and CV pairs 1..4, in track 1's scale. Any track can be any of these now (a CV out of track 1, a synth on 7).
 *
 * The core calls in through two hooks (core.h): out_note_hook, every note a track starts or ends (voice.c
 * trk_note_on / trk_note_off, a SOUND OFF track's too; a muted track starts none), and out_midi_hook, every MIDI OUT
 * message (usb.c midi_out_event). Both from the audio ISR; the platform reads what they leave from the main loop:
 *
 *   the MIDI     3-byte messages in a ring (and_out_midi_take: the page sends them on through Web MIDI, the board on its
 *                UART later). Notes, CCs, pitch bend; the clock (CLOCK OUT, off by default) is not in it yet
 *   the CV       per pair, in millivolts: 1 V an octave, 0 V at C2 (MIDI 36), 0..10 V (the DAC's range on the board: to
 *                settle with the circuit); a mono line, the last note wins and its gate is high while it sounds
 *
 * Included by and_ui.c before the pages, so it runs the same in the browser (web/andamento) and on the Daisy Seed. */

#define AO_PID 0xE500u                           /* the outputs' pids: AO_PID + AO_* (a track's) */
#define AO_PAIRS 4u                              /* CV / gate pairs on the jack strip */
#define AO_CV_MAX 10000                          /* mV */
#define AO_MIDI_RING 256u                        /* messages */
#define AO_PID_SOUND 0x271u                      /* Tessera's (pid.c): SOUND, MIDI OUT's channel */
#define AO_PID_MIDI 0x263u

enum { AO_CV, AO_NPRM };

static const char *const AO_CV_NAME[AO_PAIRS + 1u] = {"OFF", "1", "2", "3", "4"};
static const param_desc_t AO_DESC[AO_NPRM] = {
    [AO_CV] = {"CV", F_ENUM, 0, AO_PAIRS, 0, AO_CV_NAME, 0},
};

static struct {
    uint8_t cv[NTRK];                            /* each track's CV pair, 1..AO_PAIRS (0: none) */
    uint8_t note[NTRK], gate[NTRK];              /* each track's last note, and whether it still sounds (its lights, its
                                                  * lane: a SOUND OFF track has no voice to show) */
    struct {
        uint8_t note, gate, tr;                  /* the note sounding and the track that played it (gate 0: none) */
        int32_t mv;                              /* the CV now, mV (kept after the gate falls, as a sample and hold) */
        uint32_t note_ms;                        /* when its last note began */
    } pair[AO_PAIRS];
    uint8_t midi[AO_MIDI_RING][3];
    volatile uint32_t midi_w, midi_r;
} and_out;

/* ---------------------------------------------------------- settings --- */
static const param_desc_t *ao_desc(uint32_t x) { return x < AO_NPRM ? &AO_DESC[x] : 0; }
static int32_t ao_get(uint32_t x, uint32_t tr) { return x == AO_CV ? and_out.cv[tr % NTRK] : 0; }
static void ao_pair_off(uint32_t p)
{
    if (p < AO_PAIRS)
        and_out.pair[p].gate = 0;
}
static void ao_set(uint32_t x, uint32_t tr, int32_t v)
{
    uint32_t was;
    if (x != AO_CV)
        return;
    tr %= NTRK;
    was = and_out.cv[tr];
    and_out.cv[tr] = (uint8_t)clamp(v, 0, (int32_t)AO_PAIRS);
    if (was && was != and_out.cv[tr] && and_out.pair[was - 1u].tr == tr)
        ao_pair_off(was - 1u);                   /* (the note it held there ends) */
}

/* ------------------------------------------------------------ the hooks --- */
/* voice.c: track t starts note (vel 1..127) or ends it (vel 0) */
static void ao_note(const track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t tr = trk_index(t), p;
    int32_t mv;
    if (tr >= NTRK)
        return;
    if (vel)
        and_out.note[tr] = (uint8_t)note, and_out.gate[tr] = 1;
    else if (and_out.note[tr] == note)
        and_out.gate[tr] = 0;
    if (!and_out.cv[tr])
        return;
    p = and_out.cv[tr] - 1u;
    if (!vel) {
        if (and_out.pair[p].gate && and_out.pair[p].tr == tr && and_out.pair[p].note == note)
            and_out.pair[p].gate = 0;
        return;
    }
    mv = ((int32_t)note - 36) * 1000 / 12;
    and_out.pair[p].mv = mv < 0 ? 0 : mv > AO_CV_MAX ? AO_CV_MAX : mv;
    and_out.pair[p].note = (uint8_t)note;
    and_out.pair[p].tr = (uint8_t)tr;
    and_out.pair[p].gate = 1;
    and_out.pair[p].note_ms = fm1_ms | 1u;
}

/* usb.c midi_out_event: a USB-MIDI packet (cable 0: the code index in bits 0..3, the message above it) */
static void ao_midi(uint32_t pkt)
{
    uint8_t *m;
    uint32_t cin = pkt & 15u;
    if (cin < 0x8u || cin > 0xEu || cin == 0xCu || cin == 0xDu)
        return;                                  /* (3-byte messages only: notes, aftertouch, CC, bend) */
    if (and_out.midi_w - and_out.midi_r >= AO_MIDI_RING)
        return;                                  /* (full: the page reads every 2.7 ms; the newest waits out) */
    m = and_out.midi[and_out.midi_w % AO_MIDI_RING];
    m[0] = (uint8_t)(pkt >> 8);
    m[1] = (uint8_t)(pkt >> 16 & 127u);
    m[2] = (uint8_t)(pkt >> 24 & 127u);
    RING_PUBLISH();
    and_out.midi_w++;
}

/* --------------------------------------------------------- the platform --- */
/* up to max messages, 3 bytes each, into out; returns how many (main loop) */
static uint32_t and_out_midi_take(uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && and_out.midi_r != and_out.midi_w) {
        memcpy(out + 3u * n, and_out.midi[and_out.midi_r % AO_MIDI_RING], 3);
        and_out.midi_r++;
        n++;
    }
    return n;
}
/* pair p's (0..3: the jack strip's) gate and CV */
static int and_out_gate(uint32_t p) { return and_out.pair[p % AO_PAIRS].gate; }
static int32_t and_out_cv(uint32_t p) { return and_out.pair[p % AO_PAIRS].mv; }
/* track tr sounding: a voice of its own, or (SOUND OFF) a note it started and hasn't ended */
static int and_out_on(uint32_t tr)
{
    uint32_t k;
    if (tr >= NTRK)
        return 0;
    for (k = 0; k < NVOICE; k++)
        if (trk[tr].v[k].active)
            return 1;
    return and_out.gate[tr];
}
/* the CV pair track tr plays on (0..3), or -1 */
static int32_t and_out_pair_of(uint32_t tr) { return tr < NTRK && and_out.cv[tr] ? (int32_t)and_out.cv[tr] - 1 : -1; }

/* the defaults (see the top): every track's outputs, and tracks 5..8 as Andamento's generator lines. After the core's
 * own start (tessera_reset, the tracks' engines and scales): power-on, a new or loaded project */
static void and_out_defaults(void)
{
    static const uint8_t GEN[4] = {TESS_ENG_TURING, TESS_ENG_EUCLID, TESS_ENG_MARKOV, TESS_ENG_AWAKE};
    uint32_t k;
    memset(and_out.cv, 0, sizeof and_out.cv);
    memset(and_out.gate, 0, sizeof and_out.gate);
    for (k = 0; k < AO_PAIRS; k++)
        and_out.pair[k].gate = 0;
    for (k = 0; k < NTRK; k++) {
        if (k < 4u) {
            pid_set(AO_PID_MIDI, k, TESS_MIDI_OFF);
            continue;
        }
        pid_set(AO_PID_SOUND, k, 1);             /* (SOUND OFF: TX_N_SOUND) */
        pid_set(AO_PID_MIDI, k, (int32_t)(k + 1u));
        and_out.cv[k] = (uint8_t)(k - 3u);
        pid_set(PID_TESS(TX_MODE), k, GEN[k - 4u]);
        pid_set(PID_TESS(TX_EV_SEED), k, (int32_t)((k - 4u) * 37u + 11u));
        trk[k].p[P_SDIV] = k == 7u ? 1 : 2;      /* (AWAKE in eighths, the others in sixteenths: N_DIV) */
        trk[k].p[P_ROOT] = trk[0].p[P_ROOT];     /* (one scale for the song: track 1's) */
        trk[k].p[P_SCALE] = trk[0].p[P_SCALE];
    }
}

/* power-on: the hooks in, MIDI OUT sending the sequencer's notes too (MIDI_OUT_ALL: a track's MIDI CH decides), the
 * defaults */
static void and_out_init(void)
{
    memset(&and_out, 0, sizeof and_out);
    out_note_hook = ao_note;
    out_midi_hook = ao_midi;
    settings_midi_out = MIDI_OUT_ALL;
    and_out_defaults();
}
