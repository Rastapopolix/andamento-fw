/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* The parts of the FM-1's UI that Andamento's own pages (platform/andamento) use too, moved out of ui.c and
 * ui_graph.c so a build without the FM-1's UI (platform/daisy) has them: loading a sound onto a track (an engine and
 * its preset), the track's MODE (seqm_get), GEN's pattern page, the shared scale, the engines' 4-letter tags.
 * Unchanged otherwise.
 *
 * The includer provides: load_begin / load_end (ui.c: the undo levels and the cords a sound brought; the Daisy
 * build: the cords only), UNDO_SOUND and sync_reload (the web editor's RELOAD; a dummy where there is no editor). */

/* the track's MODE: 0 STEP, 1 ARP, 2.. a generator (1 + tessera.mode) */
static uint32_t seqm_get(uint32_t tr)
{
    tr %= NTRK;
    return tr < TESSERA_TRACKS && tessera.mode[tr] ? 1u + tessera.mode[tr] : trk[tr].p[P_AMODE] ? 1u : 0u;
}

static int gen_pattern_page(const page_t *pg)          /* (params.c page_id) */
{
    return pg->fam == FAM_GEN && pg->graph == GR_STEPS && seqm_get(song.sel) < 2u;
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT) leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2),
 * the scale and key map (SCL), the pattern parameters (LEN, DIV, SWING, GATE),
 * which chops whatever the track plays in time with its sequencer */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           i == P_CHRD || i == P_VOIC || i == P_MPCDEG || i == P_RECQ || param_fx(i) || param_fx_send(i);
}

/* a retired preset kept as an alias, so stored preset numbers stay valid: SAMPLE 1, once TRANH, is PIANO
 * (tools/gen_samples.py SMP_SET_ORIG). It loads as the original; browsing skips it. -> the preset k stands for */
static uint32_t preset_orig(const engine_t *e, uint32_t k) { (void)e; return k; }
static uint32_t preset_rank(const engine_t *e, uint32_t k) { (void)e; return k; }
#define preset_shown(e) (ENGINES[e]->npresets)


/* preset pi of the engine the track asked for: the sound only (not the steps, not param_kept) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0; t->user_native=0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets) {
        load_end(t);
        return;
    }
    pi = preset_orig(e, pi % e->npresets);
    t->preset = (uint8_t)pi;
    memset(&drum_patch[t-trk],0,sizeof(drum_patch_t));
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))                           /* sounds the same after any edit */
            t->p[i] = TP[i].def;
    const preset_t *pr = eng_preset(e, pi);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    fm6_track_loaded(t);                              /* FM6: the preset's patch */
    or_track_loaded(t);                               /* ORACLE: the preset's patch */
    load_end(t);
}

/* the engine's defaults and its preset pi. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void engine_load_of(track_t *t, uint32_t ei, uint32_t pi)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, pi);
    fm1_irq_on();
    load_end(t);
}
/* the engine's defaults and its first preset */
static void set_engine_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, 0u); }

/* the engine's own starting point, nothing changed: its INIT preset (FM6: "INIT .."), else
 * its first (DRUM: the 808 kit) */
static uint32_t preset_init(uint32_t e)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
    for (k = 0; k < en->npresets; k++) {
        const char *n = eng_preset(en, k)->name;
        if (n[0] == 'I' && n[1] == 'N' && n[2] == 'I' && n[3] == 'T' && (n[4] == ' ' || !n[4]))
            return k;
    }
    return 0;
}
/* Init sound (the sound's sheet, the browser's ENGINE knob): the engine's INIT, the steps
 * stay */
static void sound_init_of(track_t *t, uint32_t ei) { engine_load_of(t, ei, preset_init(ei)); }

/* SCL, QNT and the MPC degree are the song's: one scale for every part, kept the same in each track's own slots
 * (the formats and the editor keep them per track). ROOT too while KEY LINK is on (Tessera: tessera.key_link), on
 * every melodic track (a drum track keeps its own); otherwise ROOT, and TRN always, stay the track's */
static int scale_shared(uint32_t id)
{
    return id == P_SCALE || id == P_QUANT || id == P_MPCDEG || (id == P_ROOT && tessera.key_link);
}
static void scale_share(const track_t *from)
{
    uint32_t k;
    for (k = 0; k < NPART; k++) {
        trk[k].p[P_SCALE] = from->p[P_SCALE];
        trk[k].p[P_QUANT] = from->p[P_QUANT];
        trk[k].p[P_MPCDEG] = (int16_t)clamp(from->p[P_MPCDEG], 1, (int32_t)scale_count(from));
        if (tessera.key_link && !drum_track(&trk[k]) && !drum_track(from))
            trk[k].p[P_ROOT] = from->p[P_ROOT];
    }
}

/* 4-letter engine tags of the preset list */
static const char *eng_abbr(const char *name)
{
    static const char *const A[][2] = {{"SAMPLE", "SMPL"}, {"VOICE", "VOCL"}, {"TRIO", "TRIO"}, {"WHEEL", "WHEL"},
                                       {"GRAIN", "GRAN"}, {"PHYS", "PHYS"}, {"DRUM", "DRUM"},
                                       {"NOISE", "NOIS"}, {"FM6", "FM6"}, {"SLICE", "SLCE"}};
    uint32_t i;
    for (i = 0; i < sizeof A / sizeof A[0]; i++)
        if (str_eq(name, A[i][0]))
            return A[i][1];
    return name;
}
