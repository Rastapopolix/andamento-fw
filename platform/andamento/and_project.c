/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento's project file, format 1 (tessera-notes andamento/PROJECT_FORMAT.md): the music as one block of bytes,
 * for a file PROJECTS/NAME.AND on the card (the board) or a download (the browser). Plain C on the core's and the
 * UI's own calls, so the host tests, the browser and the Daisy run the same code; the card and the files are the
 * platform's (step 2). Included by and_ui.c after the settings' namespaces (and_out.c, and_master.c, and_texture.c).
 * Tessera's .tsp (TSP1, project_tsp.c) is not read here; its step packing is borrowed (project_tsp.c tsp_step_pack,
 * step v2) and its new-project defaults (project.c project_new_blank), since the Daisy build has neither file.
 *
 * The container: little-endian, RIFF-like, readable in a hex dump.
 *
 *   "ANDP" u32 n          the file; n = the bytes that follow
 *     u16 format (1) u16 0  only for a change to this container itself, never for what is in it
 *     chunk ...
 *     "CRC " u32 4 u32      zlib CRC-32 of every byte before this chunk; the last chunk
 *   chunk: 4-character id, u32 length, the bytes, a pad byte when the length is odd. A TRAK holds chunks of its own.
 *
 *   META            u32 when saved (s; 0 unknown), u8 the step format (2), u8 n, the build (n characters). Never read
 *                   for the music
 *   NAME            the name: 0..20 of A-Z, 0-9 and space (ANDP_NAME_MAX)
 *   PRMS            the song's settings as (u16 pid, i16 value) pairs: Tessera's global ones (pid.c, but the
 *                   device's and the actions), the master bus (AM_PID), the texture engine (AT_PID), AS_SEL
 *   TRAK            u8 the track (0..7), then its chunks:
 *     PRMS            the track's settings as pairs: AS_ENGINE and AS_PRESET first, then Tessera's track settings
 *                     (P_*, TX_*), the shared voice stage (AV_PID), its outputs (AO_PID: and_out.c), then AS_PATTERN.
 *                     Before the 8 tracks (2026-11-16) tracks 5..8 were and_ext.c's external tracks and saved their
 *                     settings as AE_PID pairs (and every generator's own three, AE_GPS): andp_ae maps those
 *     PATN            a pattern: u8 index, u8 step format (2), i16 LEN DIV SWING GATE, u8 DIR,
 *                     u8 steps n, n steps of ANDP_STEP_BYTES (the rest rests). An empty pattern is not written
 *
 * Reading. A file is checked whole before anything changes (andp_check: the header, every chunk inside its parent,
 * the CRC, each pattern's steps): a cut-off or damaged file is refused and the music stays as it was. Then
 * (andp_load) the transport stops, the music goes back to a new project's (project_new_blank, but every pattern
 * empty; the outputs, tracks 5..8's start, the master bus and the texture engine at their defaults), and the file is
 * applied: a chunk or a pid this build doesn't
 * know is skipped (a newer firmware wrote it), a value outside its range is clamped (the range changed), and what the
 * file doesn't hold keeps its default. So a new setting is saved the day it has a pid, with no new chunk.
 *
 * Not yet in a file (format steps 3 and 4): the engines' own patches (FM6, ORACLE, the drum kit: a track gets its
 * preset's), the cords, motion, the recording, PAT GEN, the user scales, the chord lane; the device's own settings
 * (SETTINGS.AND). The pids are Andamento's from the fork on: only ever appended, claimed in tessera-notes
 * andamento/README.md "Number claims" first. */

#define ANDP_FORMAT 1u
#define ANDP_STEP_FMT 2u                         /* the steps as TSP1's step v2 (project_tsp.c tsp_step_pack) */
#define ANDP_STEP_BYTES 10u                      /* (80 bits) */
#define ANDP_NAME_MAX 20u                        /* the page screen fits 21 characters in the Mono face */
#define ANDP_MAX 65536u                          /* the largest file this build writes or reads (a busy one: ~30 KB) */
#define ANDP_ID(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#ifndef AND_BUILD
#define AND_BUILD "dev"                          /* the firmware's git sha, from the build (META) */
#endif

/* Andamento's own save-only settings (0xE3xx), after AE_PID 0xE000, AM_PID 0xE100, AT_PID 0xE200 */
#define AS_PID 0xE300u
enum { AS_ENGINE, AS_PRESET, AS_PATTERN, AS_SEL, AS_NPRM };   /* (a track's engine by ENGI_* id, its preset, its
                                                                * pattern playing; the song's selected track 0..7) */

/* Tracks 5..8 before they were the core's (and_ext.c, until 2026-11-16): their settings' pids, AE_PID + AE_*, and each
 * generator's own three, AE_PID + AE_GPS + (gen - 1) * 3 + k. Read only, mapped onto the core's (andp_ae); never
 * written again, and never reused (tessera-notes andamento/README.md, Number claims) */
#define AE_PID 0xE000u
enum { AE_GEN, AE_P1, AE_P2, AE_P3, AE_SEED, AE_DIV, AE_OCT, AE_GATE, AE_VEL, AE_CHAN, AE_DEPTH, AE_MUTE, AE_NPRM };
#define AE_GPS 0x10u
#define AE_GENS 7u                               /* OFF TURING EUCLID MARKOV AWAKE POLY SERIES */

enum { ANDP_OK, ANDP_E_SHORT, ANDP_E_MAGIC, ANDP_E_FORMAT, ANDP_E_CHUNK, ANDP_E_CRC, ANDP_E_DATA, ANDP_E_SPACE };

static char andp_name[ANDP_NAME_MAX + 1u];      /* the music's name (loaded or saved), "" none */
static uint32_t andp_legacy;                     /* tracks read from and_ext.c's pairs (andp_ae), a bit each */

/* zlib's CRC-32, 4 bits a step (as storage.c's st_crc32, which only the FM-1's flash build has) */
static uint32_t andp_crc32(const uint8_t *b, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}
static uint32_t andp_u16(const uint8_t *b) { return b[0] | (uint32_t)b[1] << 8; }
static uint32_t andp_u32(const uint8_t *b) { return andp_u16(b) | andp_u16(b + 2) << 16; }

/* a name as a project takes it: 0..20 of A-Z, 0-9 and space (Theo, 2026-11-16: upper case and digits) */
static int andp_name_ok(const char *s, uint32_t n)
{
    uint32_t i;
    if (n > ANDP_NAME_MAX)
        return 0;
    for (i = 0; i < n; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == ' '))
            return 0;
    return 1;
}
static int andp_set_name(const char *s)
{
    uint32_t n = 0;
    while (s[n] && n <= ANDP_NAME_MAX)
        n++;
    if (!andp_name_ok(s, n))
        return 0;
    memcpy(andp_name, s, n);
    andp_name[n] = 0;
    return 1;
}

/* ------------------------------------------------------------------ steps --- */
/* step v2, 80 bits, as TSP1's (project_tsp.c tsp_step_pack): notes 4 x 7, kind 4 (n 0..4, 5 tie, 6 rest; +8
 * recorded), accent and slide 2, velocity 7, hits 8, accents / recorded gate 8, chance 8, RATCH 2, NUDGE 4, the pitch
 * accumulator 4, RATCH's high bit 1, the condition 4 */
static void andp_bits_put(uint8_t *b, uint32_t *pos, uint32_t v, uint32_t n)
{
    while (n--) {
        if (v & 1u)
            b[*pos >> 3] |= (uint8_t)(1u << (*pos & 7u));
        v >>= 1;
        (*pos)++;
    }
}
static uint32_t andp_bits_get(const uint8_t *b, uint32_t *pos, uint32_t n)
{
    uint32_t v = 0, i;
    for (i = 0; i < n; i++, (*pos)++)
        v |= (uint32_t)((b[*pos >> 3] >> (*pos & 7u)) & 1u) << i;
    return v;
}
static int andp_step_pack(uint8_t *b, const step_t *s)
{
    uint32_t pos = 0, j;
    if (s->n > 4u || s->time > ST_REST || s->vel > 127u || s->probability > 101u || s->climb == 8u || !step_acc_valid(s))
        return 0;
    memset(b, 0, ANDP_STEP_BYTES);
    for (j = 0; j < 4u; j++) {
        if (s->note[j] > 127u)
            return 0;
        andp_bits_put(b, &pos, s->note[j], 7u);
    }
    andp_bits_put(b, &pos, (s->time == ST_NOTE ? s->n : s->time == ST_TIE ? 5u : 6u) | (s->flags & SF_RECORDED ? 8u : 0u), 4u);
    andp_bits_put(b, &pos, s->flags & 3u, 2u);
    andp_bits_put(b, &pos, s->vel, 7u);
    andp_bits_put(b, &pos, s->hit, 8u);
    andp_bits_put(b, &pos, s->acc, 8u);
    andp_bits_put(b, &pos, s->probability, 8u);
    andp_bits_put(b, &pos, s->sub & 3u, 2u);
    andp_bits_put(b, &pos, (s->sub >> 2) & 15u, 4u);
    andp_bits_put(b, &pos, s->climb, 4u);
    andp_bits_put(b, &pos, (s->sub >> 6) & 1u, 1u);
    andp_bits_put(b, &pos, step_cond(s), 4u);
    return 1;
}
/* 1 when the bytes are a step that can be (s: it, kept inside its fields as project.c proj_steps keeps a load) */
static int andp_step_unpack(step_t *s, const uint8_t *b)
{
    uint32_t pos = 0, j, meta, r;
    int ok;
    memset(s, 0, sizeof *s);
    for (j = 0; j < 4u; j++)
        s->note[j] = (uint8_t)andp_bits_get(b, &pos, 7u);
    meta = andp_bits_get(b, &pos, 4u);
    s->n = (meta & 7u) < 5u ? (uint8_t)(meta & 7u) : 0u;
    s->time = (meta & 7u) < 5u ? ST_NOTE : (meta & 7u) == 5u ? ST_TIE : ST_REST;
    s->flags = (uint8_t)(andp_bits_get(b, &pos, 2u) | (meta & 8u ? SF_RECORDED : 0u));
    s->vel = (uint8_t)andp_bits_get(b, &pos, 7u);
    s->hit = (uint8_t)andp_bits_get(b, &pos, 8u);
    s->acc = (uint8_t)andp_bits_get(b, &pos, 8u);
    s->probability = (uint8_t)andp_bits_get(b, &pos, 8u);
    r = andp_bits_get(b, &pos, 2u);
    s->sub = (uint8_t)(r | andp_bits_get(b, &pos, 4u) << 2);
    s->climb = (uint8_t)andp_bits_get(b, &pos, 4u);
    s->sub |= (uint8_t)(andp_bits_get(b, &pos, 1u) << 6);
    step_set_cond(s, andp_bits_get(b, &pos, 4u));
    ok = (meta & 7u) < 7u && s->probability <= 101u && s->climb != 8u && step_acc_valid(s);
    if (s->hit)
        s->acc &= s->hit;
    else if (s->time == ST_REST)
        s->acc = 0;
    return ok;
}

/* ------------------------------------------------------------- what is saved --- */
/* a global setting kept by the device across loads (project.c GLO_KEPT: CLOCK, TUNE, MIDI, ROUTE) */
static int andp_glo_kept(uint32_t pid)
{
    static const uint8_t KEPT[4] = {G_CLOCK, G_TUNE, G_MIDI, G_ROUTE};
    uint32_t k;
    for (k = 0; k < 4u; k++)
        if (pid == PID_GLOBAL(KEPT[k]))
            return 1;
    return 0;
}
/* a pattern's own (LEN DIV SWING GATE, DIR): in its PATN, not the track's PRMS */
static int andp_per_pattern(uint32_t pid)
{
    return (pid >= P_SLEN && pid < P_SLEN + 4u) || pid == PID_TESS(TX_SEQ_DIR);
}
/* Tessera's pid e in a project: the music's (not the device's, not an action, not a pattern's), one the core can set
 * now */
static int andp_saved(const pid_entry_t *e, uint32_t tr)
{
    const param_desc_t *d;
    if (e->flags & (PID_F_ACTION | PID_F_DEVICE) || andp_glo_kept(e->pid) || andp_per_pattern(e->pid))
        return 0;
    d = pid_desc(e->pid, tr);
    return d && d->max > d->min;
}
static int32_t andp_core_get(uint32_t pid, uint32_t tr)   /* (a track's P_*: its patch's, not motion's) */
{
    return pid < PID_GLOBAL(0) ? motion_base_value(&trk[tr], pid) : pid_get(pid, tr);
}

/* ------------------------------------------------------------------ writing --- */
typedef struct {
    uint8_t *b;
    uint32_t max, pos, ok;
} andp_w_t;

static void andp_byte(andp_w_t *w, uint32_t v)
{
    if (w->pos < w->max)
        w->b[w->pos++] = (uint8_t)v;
    else
        w->ok = 0;
}
static void andp_w16(andp_w_t *w, uint32_t v) { andp_byte(w, v); andp_byte(w, v >> 8); }
static void andp_w32(andp_w_t *w, uint32_t v) { andp_w16(w, v); andp_w16(w, v >> 16); }
static uint32_t andp_begin(andp_w_t *w, uint32_t id)   /* its length filled in by andp_end */
{
    uint32_t at = w->pos;
    andp_w32(w, id);
    andp_w32(w, 0);
    return at;
}
static void andp_end(andp_w_t *w, uint32_t at)
{
    uint32_t n = w->pos - at - 8u;
    if (!w->ok)
        return;
    w->b[at + 4] = (uint8_t)n;
    w->b[at + 5] = (uint8_t)(n >> 8);
    w->b[at + 6] = (uint8_t)(n >> 16);
    w->b[at + 7] = (uint8_t)(n >> 24);
    if (n & 1u)
        andp_byte(w, 0);
}
static void andp_pair(andp_w_t *w, uint32_t pid, int32_t v)
{
    andp_w16(w, pid);
    andp_w16(w, (uint32_t)(int16_t)v);
}
/* the header (call first); andp_finish closes the file with its CRC: its length, 0 when it didn't fit */
static void andp_start(andp_w_t *w, uint8_t *out, uint32_t max)
{
    w->b = out, w->max = max, w->pos = 0, w->ok = 1;
    andp_w32(w, ANDP_ID('A', 'N', 'D', 'P'));
    andp_w32(w, 0);
    andp_w16(w, ANDP_FORMAT);
    andp_w16(w, 0);
}
static uint32_t andp_finish(andp_w_t *w)
{
    uint32_t n = w->pos + 12u - 8u;              /* (the length counts the CRC chunk to come) */
    if (!w->ok || w->pos + 12u > w->max)
        return 0;
    w->b[4] = (uint8_t)n;
    w->b[5] = (uint8_t)(n >> 8);
    w->b[6] = (uint8_t)(n >> 16);
    w->b[7] = (uint8_t)(n >> 24);
    n = andp_crc32(w->b, w->pos);
    andp_w32(w, ANDP_ID('C', 'R', 'C', ' '));
    andp_w32(w, 4);
    andp_w32(w, n);
    return w->ok ? w->pos : 0;
}

/* track tr's pattern b, as it is now (the one playing: the track's own steps and LEN DIV SWING GATE) */
static const step_t *andp_steps(uint32_t tr, uint32_t b, const int16_t **timing)
{
    if (b == trk[tr].pattern) {
        *timing = &trk[tr].p[P_SLEN];
        return trk[tr].step;
    }
    *timing = pattern_at(tr, b)->timing;
    return pattern_at(tr, b)->step;
}
static int andp_pattern_empty(uint32_t tr, uint32_t b)
{
    const int16_t *tm;
    const step_t *s = andp_steps(tr, b, &tm);
    uint32_t i;
    if (pattern_dir[tr][b])
        return 0;
    for (i = 0; i < 4u; i++)
        if (tm[i] != TP[P_SLEN + i].def)
            return 0;
    for (i = 0; i < NSTEP; i++) {
        step_t r;
        memset(&r, 0, sizeof r);
        r.time = ST_REST;
        if (memcmp(&s[i], &r, sizeof r))
            return 0;
    }
    return 1;
}

/* the music as it is now -> out (max bytes): the file's length, 0 when it didn't fit or a step can't be packed */
static uint32_t andp_save(uint8_t *out, uint32_t max)
{
    andp_w_t w;
    uint32_t c, t, i, k, n;
    andp_start(&w, out, max);

    c = andp_begin(&w, ANDP_ID('M', 'E', 'T', 'A'));
    andp_w32(&w, 0);                             /* (when saved: the board's clock, later) */
    andp_byte(&w, ANDP_STEP_FMT);
    n = (uint32_t)strlen(AND_BUILD);
    andp_byte(&w, n);
    for (i = 0; i < n; i++)
        andp_byte(&w, (uint8_t)AND_BUILD[i]);
    andp_end(&w, c);

    c = andp_begin(&w, ANDP_ID('N', 'A', 'M', 'E'));
    for (i = 0; andp_name[i]; i++)
        andp_byte(&w, (uint8_t)andp_name[i]);
    andp_end(&w, c);

    c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));   /* the song's */
    for (i = 0; i < pid_count(); i++) {
        const pid_entry_t *e = pid_at(i);
        if (!(e->flags & PID_F_TRACK) && andp_saved(e, 0))
            andp_pair(&w, e->pid, andp_core_get(e->pid, 0));
    }
    for (k = 0; k < AM_NPRM; k++)
        andp_pair(&w, AM_PID + k, am_get(k));
    for (k = 0; k < AT_NPRM; k++)
        andp_pair(&w, AT_PID + k, at_get(k));
    andp_pair(&w, AS_PID + AS_SEL, (int32_t)AND_SEL);
    andp_end(&w, c);

    for (t = 0; t < AND_TRACKS; t++) {
        uint32_t tc = andp_begin(&w, ANDP_ID('T', 'R', 'A', 'K'));
        andp_byte(&w, t);
        c = andp_begin(&w, ANDP_ID('P', 'R', 'M', 'S'));
        andp_pair(&w, AS_PID + AS_ENGINE, trk[t].eng_req);
        andp_pair(&w, AS_PID + AS_PRESET, trk[t].preset);
        for (i = 0; i < pid_count(); i++) {
            const pid_entry_t *e = pid_at(i);
            if ((e->flags & PID_F_TRACK) && andp_saved(e, t))
                andp_pair(&w, e->pid, andp_core_get(e->pid, t));
        }
        for (k = 0; k < VS_NPRM; k++)             /* the shared voice stage (vstage.c) */
            andp_pair(&w, AV_PID + k, vs_get(t, k));
        for (k = 0; k < AO_NPRM; k++)             /* its outputs (and_out.c) */
            andp_pair(&w, AO_PID + k, ao_get(k, t));
        andp_pair(&w, AS_PID + AS_PATTERN, trk[t].pattern);
        andp_end(&w, c);
        for (k = 0; k < NPAT; k++) {
            const int16_t *tm;
            const step_t *s;
            if (andp_pattern_empty(t, k))
                continue;
            s = andp_steps(t, k, &tm);
            c = andp_begin(&w, ANDP_ID('P', 'A', 'T', 'N'));
            andp_byte(&w, k);
            andp_byte(&w, ANDP_STEP_FMT);
            for (i = 0; i < 4u; i++)
                andp_w16(&w, (uint32_t)(uint16_t)tm[i]);
            andp_byte(&w, pattern_dir[t][k]);
            andp_byte(&w, NSTEP);
            for (i = 0; i < NSTEP; i++) {
                uint8_t v[ANDP_STEP_BYTES];
                if (!andp_step_pack(v, &s[i]))
                    return 0;
                for (n = 0; n < ANDP_STEP_BYTES; n++)
                    andp_byte(&w, v[n]);
            }
            andp_end(&w, c);
        }
        andp_end(&w, tc);
    }
    return andp_finish(&w);
}

/* ------------------------------------------------------------------ reading --- */
/* the chunk at *pos inside [.., end): 1 with its id, body and length (and *pos past it), 0 at end, -1 when it runs
 * past end */
static int andp_next(const uint8_t *b, uint32_t end, uint32_t *pos, uint32_t *id, const uint8_t **body, uint32_t *n)
{
    uint32_t p = *pos, len;
    if (p >= end)
        return 0;
    if (end - p < 8u)
        return -1;
    *id = andp_u32(b + p);
    len = andp_u32(b + p + 4);
    if (len > end - p - 8u)
        return -1;
    *body = b + p + 8u;
    *n = len;
    p += 8u + len;
    if (len & 1u) {                              /* (the pad byte: the last chunk of its parent may lack it) */
        if (p < end)
            p++;
    }
    *pos = p;
    return 1;
}

/* a pattern chunk: 1 when it reads (its steps checked; *use 0 when this build skips it: an index or a step format it
 * doesn't know) */
static int andp_patn_ok(const uint8_t *b, uint32_t n, int *use)
{
    uint32_t i, ns;
    *use = 0;
    if (n < 12u)
        return 0;
    ns = b[11];
    if (n < 12u + ns * ANDP_STEP_BYTES)
        return 0;
    if (b[0] >= NPAT || b[1] != ANDP_STEP_FMT || ns > NSTEP)
        return 1;                                /* (a newer firmware's: skipped) */
    for (i = 0; i < ns; i++) {
        step_t s;
        if (!andp_step_unpack(&s, b + 12u + i * ANDP_STEP_BYTES))
            return 0;
    }
    *use = 1;
    return 1;
}

/* b (len bytes) is a whole Andamento project this build can load: ANDP_OK, else why not (nothing is changed) */
static int andp_check(const uint8_t *b, uint32_t len)
{
    uint32_t pos = 12u, id, n, crc_at = 0;
    const uint8_t *body;
    int r;
    if (len < 12u)
        return ANDP_E_SHORT;
    if (andp_u32(b) != ANDP_ID('A', 'N', 'D', 'P'))
        return ANDP_E_MAGIC;
    if (andp_u32(b + 4) != len - 8u)
        return ANDP_E_SHORT;                     /* (cut off, or with something after it) */
    if (andp_u16(b + 8) != ANDP_FORMAT)
        return ANDP_E_FORMAT;
    while ((r = andp_next(b, len, &pos, &id, &body, &n)) > 0) {
        if (crc_at)
            return ANDP_E_CHUNK;                 /* (the CRC is the last chunk) */
        if (id == ANDP_ID('C', 'R', 'C', ' ')) {
            if (n != 4u)
                return ANDP_E_CHUNK;
            crc_at = (uint32_t)(body - b) - 8u;
        } else if (id == ANDP_ID('N', 'A', 'M', 'E')) {
            if (!andp_name_ok((const char *)body, n))
                return ANDP_E_DATA;
        } else if (id == ANDP_ID('P', 'R', 'M', 'S')) {
            if (n & 3u)
                return ANDP_E_CHUNK;
        } else if (id == ANDP_ID('T', 'R', 'A', 'K')) {
            uint32_t sp = 1u, sid, sn;
            const uint8_t *sb;
            int sr;
            if (n < 1u)
                return ANDP_E_CHUNK;
            while ((sr = andp_next(body, n, &sp, &sid, &sb, &sn)) > 0) {
                int use;
                if (sid == ANDP_ID('P', 'R', 'M', 'S') && (sn & 3u))
                    return ANDP_E_CHUNK;
                if (sid == ANDP_ID('P', 'A', 'T', 'N') && !andp_patn_ok(sb, sn, &use))
                    return ANDP_E_DATA;
            }
            if (sr < 0)
                return ANDP_E_CHUNK;
        }
    }
    if (r < 0)
        return ANDP_E_CHUNK;
    if (!crc_at)
        return ANDP_E_SHORT;
    return andp_crc32(b, crc_at) == andp_u32(b + crc_at + 8u) ? ANDP_OK : ANDP_E_CRC;
}

/* the music back to a new project's, as project.c project_new_blank (which the Daisy build lacks) but with every
 * pattern empty (its demo steps are not a file's): the tracks their power-on engines and sounds, the song's settings
 * their defaults but the device's (CLOCK TUNE MIDI ROUTE), no cords; the outputs and tracks 5..8's start (and_out.c),
 * the master bus and the texture engine at theirs. The transport stops first, every voice let go */
static void andp_defaults(void)
{
    uint32_t i, k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    seq_stop();
    transport_req = 0;
    tessera_stop_all();
    tessera_reset();
    tune_sel = 0;                                /* (TUNING, the project's: tessera_reset leaves it) */
    fm1_irq_off();
    chain_defaults(&chain_config);
    pattern_init();
    fm1_irq_on();
    for (i = 0; i < G_COUNT; i++)
        if (!andp_glo_kept(PID_GLOBAL(i)))
            song.g[i] = GP[i].def;
    TRK_DEF_SONG();
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        for (i = 0; i < P_E0; i++)
            t->p[i] = TP[i].def;
        set_engine_of(t, TRK_DEF[k][0]);
        apply_preset_to(t, TRK_DEF[k][1]);
        if (TRK_DEF[k][0] != ENGI_DRUM)
            TRK_DEF_SCALE_OF(t);
        pattern_apply(t, 0);
        vs_defaults(k);
    }
    song.sel = 0;
    cord_defaults();
    and_out_defaults();
    for (k = 0; k < AM_NPRM; k++)
        am_set(k, AM_DESC[k].def);
    for (k = 0; k < AT_NPRM; k++)
        at_set(k, AT_DESC[k].def);
    and_select(0);
    andp_name[0] = 0;
}

/* a pair of tracks 5..8 as and_ext.c saved them (before 2026-11-16) onto the core's settings: the generator, its three,
 * SEED, DIV, OCT (as TRANSPOSE, at most 2 octaves), GATE, MIDI CHAN, ENERGY DEPTH, MUTE. VEL has no core setting (the
 * generator's velocity plays as it is). The lines are the core's generators from here, so they play alike, not the
 * same notes. 1: it was one */
static int andp_ae(uint32_t pid, int32_t v, uint32_t tr)
{
    static const uint8_t MODE[AE_GENS] = {TESS_ENG_OFF, TESS_ENG_TURING, TESS_ENG_EUCLID, TESS_ENG_MARKOV,
                                          TESS_ENG_AWAKE, TESS_ENG_POLY, TESS_ENG_SERIES};
    static const uint8_t GP[AE_GENS - 1u][3] = {
        {TX_TUR_LEN, TX_TUR_PROB, TX_TUR_SPAN}, {TX_EUC_STEPS, TX_EUC_PULSES, TX_EUC_ROT},
        {TX_MKV_LEAP, TX_MKV_DENS, TX_MKV_SPAN}, {TX_AWK_LENA, TX_AWK_LENB, TX_AWK_PROB},
        {TX_POL_DIV1, TX_POL_DIV2, TX_POL_SPAN}, {TX_SR_START, TX_SR_INTV, TX_SR_LOOP}};
    static const uint8_t DIV[6] = {0, 1, 4, 2, 5, 3};   /* (1/4 1/8 1/8T 1/16 1/16T 1/32 as params.c N_DIV) */
    uint32_t x;
    if (pid < AE_PID || pid >= AE_PID + AE_GPS + 3u * (AE_GENS - 1u) || (pid >= AE_PID + AE_NPRM && pid < AE_PID + AE_GPS))
        return 0;
    x = pid - AE_PID;
    if (x >= AE_GPS) {
        x -= AE_GPS;
        pid_set(PID_TESS(GP[x / 3u][x % 3u]), tr, v);
        return 1;
    }
    switch (x) {
    case AE_GEN: pid_set(PID_TESS(TX_MODE), tr, MODE[clamp(v, 0, AE_GENS - 1)]); break;
    case AE_SEED: pid_set(PID_TESS(TX_EV_SEED), tr, 1 + clamp(v, 0, 255) % 128); break;
    case AE_DIV: trk[tr].p[P_SDIV] = DIV[clamp(v, 0, 5)]; break;
    case AE_OCT: pid_set(PID_TRACK(P_TRANS), tr, 12 * v); break;
    case AE_GATE: pid_set(PID_TRACK(P_SGATE), tr, clamp(v, 5, 100) * 127 / 100); break;
    case AE_CHAN: pid_set(PID_TESS(TX_MIDI_CH), tr, v > 0 ? clamp(v, 1, 16) : (int32_t)TESS_MIDI_OFF); break;
    case AE_DEPTH: pid_set(PID_TESS(TX_EN_DEPTH), tr, v); break;
    case AE_MUTE: pid_set(PID_TRACK(P_MUTE), tr, v != 0); break;
    default: break;                              /* (AE_P1..P3, the pages' view, never saved; AE_VEL: none) */
    }
    return 1;
}

/* one (pid, value) pair of the song's PRMS (tr: AND_TRACKS) or track tr's; unknown pids are skipped */
static void andp_apply(uint32_t pid, int32_t v, uint32_t tr)
{
    if (tr >= AND_TRACKS) {
        if (pid >= AM_PID && pid < AM_PID + AM_NPRM)
            am_set(pid - AM_PID, v);
        else if (pid >= AT_PID && pid < AT_PID + AT_NPRM)
            at_set(pid - AT_PID, v);
        else if (pid == AS_PID + AS_SEL)
            and_select((uint32_t)clamp(v, 0, AND_TRACKS - 1));
        else if (pid < AND_OWN_PID && !andp_glo_kept(pid)) {
            const pid_entry_t *e = pid_find(pid);
            if (e && !(e->flags & PID_F_TRACK))
                pid_set(pid, 0, v);              /* (clamped; the device's and the actions refused) */
        }
    } else if (pid >= AV_PID && pid < AV_PID + VS_NPRM) {
        vs_set(tr, pid - AV_PID, v);
    } else if (pid >= AO_PID && pid < AO_PID + AO_NPRM) {
        ao_set(pid - AO_PID, tr, v);
    } else if (andp_ae(pid, v, tr)) {
        andp_legacy |= 1u << tr;
    } else if (pid < AND_OWN_PID && !andp_per_pattern(pid)) {
        const pid_entry_t *e = pid_find(pid);
        if (e && (e->flags & PID_F_TRACK))
            pid_set(pid, tr, v);
    }
}

/* a track's TRAK (body, n bytes, checked): its engine and preset first (they set the patch), then its settings over
 * them, then its patterns and the one playing */
static void andp_trak(uint32_t tr, const uint8_t *body, uint32_t n)
{
    uint32_t sp, sid, sn, i;
    const uint8_t *sb;
    int32_t engine = -1, preset = -1, pattern = 0;
    track_t *t = &trk[tr % NTRK];
    tr %= NTRK;
    for (sp = 1u; andp_next(body, n, &sp, &sid, &sb, &sn) > 0;)
        for (i = 0; sid == ANDP_ID('P', 'R', 'M', 'S') && i + 4u <= sn; i += 4u) {
            int32_t v = (int16_t)andp_u16(sb + i + 2u);
            switch (andp_u16(sb + i)) {
            case AS_PID + AS_ENGINE: engine = v; break;
            case AS_PID + AS_PRESET: preset = v; break;
            case AS_PID + AS_PATTERN: pattern = v; break;
            }
        }
    if (engine >= 0) {
        uint32_t e = (uint32_t)engine < NENGINES && ENG_BUILT(engine) ? (uint32_t)engine : ENGI_FALLBACK;
        if (e != t->eng_req)                     /* (an engine this build lacks: the fallback, as TSP1) */
            set_engine_of(t, e);
        if (preset >= 0 && ENGINES[e]->npresets)
            apply_preset_to(t, (uint32_t)preset % ENGINES[e]->npresets);
    }
    for (sp = 1u; andp_next(body, n, &sp, &sid, &sb, &sn) > 0;)
        for (i = 0; sid == ANDP_ID('P', 'R', 'M', 'S') && i + 4u <= sn; i += 4u)
            andp_apply(andp_u16(sb + i), (int16_t)andp_u16(sb + i + 2u), tr);
    for (sp = 1u; andp_next(body, n, &sp, &sid, &sb, &sn) > 0;) {
        int use;
        pattern_t *p;
        if (sid != ANDP_ID('P', 'A', 'T', 'N') || (andp_patn_ok(sb, sn, &use), !use))
            continue;
        p = pattern_at(tr, sb[0]);
        for (i = 0; i < 4u; i++)
            p->timing[i] = (int16_t)clamp((int16_t)andp_u16(sb + 2u + 2u * i), TP[P_SLEN + i].min, TP[P_SLEN + i].max);
        pattern_dir[tr][sb[0]] = (uint8_t)(sb[10] < SEQ_DIRS ? sb[10] : 0u);
        for (i = 0; i < NSTEP; i++) {
            memset(&p->step[i], 0, sizeof p->step[i]);
            p->step[i].time = ST_REST;
            if (i < sb[11])
                (void)andp_step_unpack(&p->step[i], sb + 12u + i * ANDP_STEP_BYTES);
        }
    }
    pattern_apply(t, (uint32_t)clamp(pattern, 0, NPAT - 1));
}

/* b (len bytes) -> the music: ANDP_OK, else andp_check's refusal and nothing changed */
static int andp_load(const uint8_t *b, uint32_t len)
{
    uint32_t pos = 12u, id, n, i;
    const uint8_t *body;
    int r = andp_check(b, len);
    if (r != ANDP_OK)
        return r;
    andp_defaults();
    andp_legacy = 0;
    while (andp_next(b, len, &pos, &id, &body, &n) > 0) {
        if (id == ANDP_ID('N', 'A', 'M', 'E')) {
            memcpy(andp_name, body, n);
            andp_name[n] = 0;
        } else if (id == ANDP_ID('P', 'R', 'M', 'S')) {
            for (i = 0; i + 4u <= n; i += 4u)
                andp_apply(andp_u16(body + i), (int16_t)andp_u16(body + i + 2u), AND_TRACKS);
        } else if (id == ANDP_ID('T', 'R', 'A', 'K') && body[0] < AND_TRACKS)
            andp_trak(body[0], body, n);
    }
    for (i = 0; i < NTRK; i++)                   /* (and_ext.c's tracks played in track 1's scale) */
        if (andp_legacy >> i & 1u) {
            trk[i].p[P_ROOT] = trk[0].p[P_ROOT];
            trk[i].p[P_SCALE] = trk[0].p[P_SCALE];
        }
    return ANDP_OK;
}
