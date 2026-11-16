/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's shared voice stage: engine -> FILTER -> DRIVE -> AMP, the same on every engine (andamento repo,
 * docs/PLAN.md "Sound engines": SOUND's FILTER and AMP pages behave alike whichever engine a track plays). Included by
 * voice.c, which calls it for each voice it renders. Before this stage each engine had only its own filter, and the
 * voice's cutoff modulation (ENV and LFO to FILTER, accent, the matrix's CUTOFF) nudged whatever the engine treats as
 * its tone; with TYPE on, that modulation moves this filter instead and the engine's own tone stays where its knobs
 * put it.
 *
 *   TYPE    OFF, LP (2-pole), LP4 (4-pole), BP, HP: a trapezoidal state-variable filter (A. Simper's), per voice,
 *           in float (the Daisy's FPU); LP4 is two of them, Butterworth-tuned, the resonance on the second
 *   CUT     cutoff 30 Hz .. 16 kHz, Tessera's CUTOFF scale (tools/gen_tables.py CUTOFF_HZ); plus the voice's
 *           cutoff modulation and key tracking, once a control block (CTL samples)
 *   RES     resonance, up to the edge of self-oscillation
 *   DRIVE   a soft saturator after the filter: up to 16 x into a tanh-like curve, its output trimmed by g^-1/4 (quiet
 *           parts come up, peaks go down a little, so turning it keeps the level roughly in place)
 *   KEY     key tracking: 100 % moves the cutoff an octave per octave played, around middle C (note 60)
 *   VEL     how far velocity sets the amplitude: 100 % (the default) as Tessera always did, 0 % every note full
 *
 * With TYPE OFF and DRIVE 0 the voice renders exactly as before (the engine applies its own amplitude ramp and
 * nothing here runs), and VEL 100 % is the same arithmetic as before: the default leaves every sound, and the
 * golden renders, bit-identical. When the stage is on, the engine renders at a steady full amplitude into a block of
 * its own, then filter, drive and the amplitude ramp (envelope, velocity, LFO to AMP, fades) follow in that order.
 * Engines with their own envelopes (engine_t.ownenv: FM6) keep them: they render with their amplitude as before, and
 * filter and drive follow it.
 *
 * The settings are a track's, one set per track (not in track_t.p: no change to Tessera's P_* layout, the user
 * presets or TSP1); Andamento's UI and project file reach them as pids AV_PID + VS_* (platform/andamento). */

enum { VS_TYPE, VS_CUT, VS_RES, VS_DRIVE, VS_KEY, VS_VEL, VS_NPRM };
enum { VS_OFF, VS_LP, VS_LP4, VS_BP, VS_HP };

static const char *const VS_TYPES[] = {"OFF", "LP", "LP4", "BP", "HP", 0};
static const param_desc_t VS_DESC[VS_NPRM] = {
    [VS_TYPE] = {"TYPE", F_ENUM, VS_OFF, VS_HP, VS_OFF, VS_TYPES, 0},
    [VS_CUT] = {"CUT", F_CUTOFF, 0, 127, 100, 0, 0},           /* (100: ~4 kHz, open but not wide open) */
    [VS_RES] = {"RES", F_INT, 0, 127, 0, 0, 0},
    [VS_DRIVE] = {"DRIVE", F_PCT, 0, 100, 0, 0, 0},
    [VS_KEY] = {"KEY", F_PCT, 0, 100, 0, 0, 0},
    [VS_VEL] = {"VEL", F_PCT, 0, 100, 100, 0, 0},
};
static const char *const VS_NAME[VS_NPRM] = {"FILTER TYPE", "CUTOFF", "RESONANCE", "DRIVE", "KEY TRACK", "VELOCITY"};

#define VS_DEFAULTS {VS_OFF, 100, 0, 0, 0, 100}
static int16_t vs_p[NTRK][VS_NPRM] = {[0 ... NTRK - 1] = VS_DEFAULTS};
static float vs_z[NTRK][NVOICE][4];                    /* each voice's filter state: two SVFs' two integrators */

static const param_desc_t *vs_desc(uint32_t x) { return x < VS_NPRM ? &VS_DESC[x] : 0; }
static int32_t vs_get(uint32_t tr, uint32_t x) { return tr < NTRK && x < VS_NPRM ? vs_p[tr][x] : 0; }
static void vs_set(uint32_t tr, uint32_t x, int32_t v)
{
    if (tr < NTRK && x < VS_NPRM)
        vs_p[tr][x] = (int16_t)clamp(v, VS_DESC[x].min, VS_DESC[x].max);
}
static void vs_defaults(uint32_t tr)
{
    uint32_t x;
    for (x = 0; x < VS_NPRM; x++)
        vs_set(tr, x, VS_DESC[x].def);
}

/* the stage runs for track tr's voices (TYPE on, or DRIVE up); the filter takes the voice's cutoff modulation */
static int vs_on(uint32_t tr) { return tr < NTRK && (vs_p[tr][VS_TYPE] != VS_OFF || vs_p[tr][VS_DRIVE] > 0); }
static int vs_filter_on(uint32_t tr) { return tr < NTRK && vs_p[tr][VS_TYPE] != VS_OFF; }

/* the voice amplitude's velocity factor (Q15-ish, as voice.c always had it: vel * 258, 127 -> 32766) */
static int32_t vs_vel_gain(uint32_t tr, uint32_t vel)
{
    int32_t s = tr < NTRK ? vs_p[tr][VS_VEL] : 100;
    if (s >= 100)
        return (int32_t)vel * 258;                     /* (the default: exactly as before) */
    return 127 * 258 - (127 - (int32_t)vel) * 258 * s / 100;
}

/* a voice starts from silence: its filter starts from rest (a retrigger or steal keeps it, as the engines keep
 * theirs: resetting a ringing filter clicks) */
static void vs_voice_reset(uint32_t tr, uint32_t vi)
{
    if (tr < NTRK && vi < NVOICE)
        vs_z[tr][vi][0] = vs_z[tr][vi][1] = vs_z[tr][vi][2] = vs_z[tr][vi][3] = 0.0f;
}

/* the filter's cutoff for a voice this block, 0..127 << 8: CUT, the voice's modulation (m->cutoff: ENV and LFO to
 * FILTER, accent, the matrix) and key tracking from its pitch now (pitch16, 1/16 semitone; 960 is note 60). KEY
 * 100 %: 14.02 steps of CUT an octave (127 steps over 30 Hz .. 16 kHz, log2(16000 / 30) = 9.06 octaves), so
 * 256 * 127 / (9.06 * 192) = 18.69 units of 1/256 a 1/16 semitone */
static int32_t vs_cutoff(uint32_t tr, int32_t mcut, int32_t pitch16)
{
    int32_t c = ((int32_t)vs_p[tr][VS_CUT] << 8) + mcut + (pitch16 - 960) * vs_p[tr][VS_KEY] * 1869 / 10000;
    return clamp(c, 0, 127 << 8);
}

/* tanh-like, 1 at |x| = 3 and flat beyond (a Pade fit: no table, no libm) */
static inline float vs_sat(float x)
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/* one voice's block (buf, n samples at the voice level, VOICE_FS) through the stage, in place: filter (cut: from
 * vs_cutoff), drive, then the amplitude ramp m->amp0 .. m->amp1 when amp (the engine rendered at full amplitude) */
static void vs_voice(uint32_t tr, uint32_t vi, int32_t *buf, uint32_t n, int32_t cut, const vmod_t *m, int amp)
{
    const int16_t *p = vs_p[tr];
    float *z = vs_z[tr][vi];
    uint32_t i, type = (uint32_t)p[VS_TYPE];
    float g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, kb = 2.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
    float drv = 1.0f + (float)p[VS_DRIVE] * 0.15f, trim = 1.0f;
    const float in_scale = 1.0f / (float)VOICE_FS, out_scale = (float)VOICE_FS;
    if (type != VS_OFF) {
        /* CUTOFF_HZ: 30 * (16000 / 30) ^ (v / 127), v here in 1/256 steps; g = tan(pi fc / FS) */
        float hz = 30.0f * expf((float)cut * (6.27919f / (127.0f * 256.0f)));
        float res = (float)p[VS_RES] * (1.0f / 127.0f);
        if (hz > 0.45f * (float)FS)
            hz = 0.45f * (float)FS;
        g = tanf(3.14159265f * hz / (float)FS);
        if (type == VS_LP4) {
            k = 1.84776f;                              /* 4-pole Butterworth: 2 cos(pi/8), 2 cos(3 pi/8) */
            kb = 0.76537f * (1.0f - 0.97f * res);
        } else
            k = 2.0f - 1.95f * res;                    /* 2 (no peak) .. 0.05 (ringing, still stable) */
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        b1 = 1.0f / (1.0f + g * (g + kb));
        b2 = g * b1;
        b3 = g * b2;
    }
    if (p[VS_DRIVE] > 0)
        trim = 1.0f / sqrtf(sqrtf(drv));
    for (i = 0; i < n; i++) {
        float x = (float)buf[i] * in_scale, y = x;
        if (type != VS_OFF) {
            float v3 = x - z[1], v1 = a1 * z[0] + a2 * v3, v2 = z[1] + a2 * z[0] + a3 * v3;
            z[0] = 2.0f * v1 - z[0];
            z[1] = 2.0f * v2 - z[1];
            y = type == VS_BP ? v1 : type == VS_HP ? x - k * v1 - v2 : v2;
            if (type == VS_LP4) {
                float w3 = y - z[3], w1 = b1 * z[2] + b2 * w3, w2 = z[3] + b2 * z[2] + b3 * w3;
                z[2] = 2.0f * w1 - z[2];
                z[3] = 2.0f * w2 - z[3];
                y = w2;
            }
        }
        if (p[VS_DRIVE] > 0)
            y = vs_sat(y * drv) * trim;
        if (amp)
            y *= (float)amp_at(m, i) * (1.0f / 32768.0f);
        y *= out_scale;
        buf[i] = y > 1.0e9f ? 1000000000 : y < -1.0e9f ? -1000000000 : (int32_t)y;
    }
    if (!(z[0] == z[0]) || !(z[2] == z[2]))            /* (NaN: a state gone bad starts again from rest) */
        z[0] = z[1] = z[2] = z[3] = 0.0f;
}
