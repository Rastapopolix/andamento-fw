/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento: Tessera's portable core on a Daisy Seed (STM32H750, libDaisy), first stage.
 *
 * A unity build, as firmware/src/melodee.c and tests/hostsim.c are: the engines, voices, mods, the mixer, the
 * sequencer and the generators, with the FM-1's hardware names (fm1_*) given plain stand-ins here, as the host
 * tests give them, and Andamento's panel UI (platform/andamento/and_ui.c, with tracks 5..8, the master bus and the
 * texture engine). No storage or USB yet. main.cpp hands this file one block of CTL samples at a time from libDaisy's
 * audio callback, the panel's inputs as they come, and asks for the screens about 30 times a second. Until the first
 * input from the panel, a short figure plays on track 1, so the board makes sound on day one with nothing wired.
 *
 * Built at FS = 48000 (platform/daisy/Makefile: MELODEE_FS for tools/gen_tables.py), the Daisy's codec rate.
 * The FM-1's placements are kept (section, noinline, always_inline): tessera_daisy.lds puts its .audio_text and
 * .dsp_text in ITCM and its .dsp_tables in DTCM, with the rest of the audio path, and .pool and .noinit in .bss.
 * The include list follows tests/hostsim.c; keep the two in step when a source is added there. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define memset melodee_memset
#define memcpy melodee_memcpy
#define memcmp melodee_memcmp
#include "melodee_tables.h"
#include "../../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;   /* the keys: bit k = note key k (7 = C4) */
#include "../../firmware/src/core.h"
#include "../../firmware/src/engines.c"
#include "../../firmware/src/params.c"
#include "../../firmware/src/mod.c"
#include "../../firmware/src/voice.c"
#include "../../firmware/src/mix.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#if MELODEE_USB_HOST
static void fm1_wdt_feed(void) {}
#endif
#include "../../firmware/src/usb.c"
#include "../../firmware/src/midi_uart.c"
#include "../../firmware/src/tessera.h"
#include "../../firmware/src/gen.c"
#include "../../firmware/src/song_chain.c"
#include "../../firmware/src/seq.c"
/* The main loop's guard against the audio callback (an interrupt): PRIMASK off around a change the callback must
 * not see half made, nesting, back to what it was at the outermost ON. Only tessera_core_tick_ms runs in the main
 * loop yet; the UI will need this. */
static uint32_t daisy_irq_depth, daisy_irq_primask;
static inline __attribute__((always_inline)) void daisy_irq_off(void)
{
    uint32_t pm;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
    if (!daisy_irq_depth++)
        daisy_irq_primask = pm;
}
static inline __attribute__((always_inline)) void daisy_irq_on(void)
{
    if (daisy_irq_depth && !--daisy_irq_depth && !daisy_irq_primask)
        __asm__ volatile("cpsie i" ::: "memory");
}
#define TESS_IRQ_OFF() daisy_irq_off()
#define TESS_IRQ_ON() daisy_irq_on()
#define fm1_irq_off() daisy_irq_off()               /* (the FM-1's names, as ui.c's loads call them) */
#define fm1_irq_on() daisy_irq_on()
#define TESS_ENTROPY() (fm1_ms * 2654435761u)
#include "../../firmware/src/tessera.c"
#include "../../firmware/src/pat_gen.c"
#include "../../firmware/src/pid.c"
#include "../../firmware/src/cord.c"
#include "../../firmware/src/mscope.c"
#include "../../firmware/src/mo_font.h"
#include "../andamento/and_fb.c"                  /* the screen: a framebuffer of 16 levels */
#include "../andamento/ssd1322.c"                 /* .. as the SSD1322's bytes (and_spi_cmd / and_spi_data: main.cpp) */
/* Loading a sound (an engine, a preset) as the FM-1's UI does it, for and_pages.c's ENGINE and PRESET: without the
 * FM-1's undo levels, which belong to its UI; the cords a sound brought still go (cord.c), as on the FM-1 */
enum { UNDO_SOUND = 1, UNDO_PAT = 2 };
static uint8_t sync_reload;                      /* (the web editor's RELOAD: no editor here) */
static void load_begin(track_t *t, uint32_t what)
{
    if (what & UNDO_SOUND)
        cord_sound_load(trk_index(t));
}
static void load_end(track_t *t) { (void)t; }
#include "../../firmware/src/ui_common.c"
#define AND_SDRAM __attribute__((section(".sdram_bss")))   /* the texture engine's 2 s capture (and_texture.c) */
#include "../andamento/and_ui.c"                  /* Andamento's panel: the two screens, encoders, keys (and its parts) */

#include "tessera_core.h"

_Static_assert(FS == 48000, "platform/daisy: build the tables at MELODEE_FS=48000");

static uint32_t daisy_frame;                     /* samples played since tessera_core_init */
static volatile uint8_t daisy_demo;              /* the figure plays until the panel's first input */

void tessera_core_init(void)
{
    uint32_t i, k;
    track_t *t = &trk[0];
    dsp_tables_init();                           /* the RAM tables (fm1_main on the FM-1) */
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++)                   /* (every track's common settings; tracks 5..8: and_out.c's start) */
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
    t->eng_req = t->engine = ENGI_FM6;           /* FM6, its first preset, as tests/hostsim.c */
    for (i = 0; i < 8; i++)
        t->p[P_E0 + i] = ENGINES[ENGI_FM6]->edit[i].def;
    if (ENGINES[ENGI_FM6]->npresets) {
        const preset_t *p = eng_preset(ENGINES[ENGI_FM6], 0);
        for (i = 0; i < 8; i++)
            t->p[P_E0 + i] = p->e[i];
        t->p[P_ATK] = p->env[0];
        t->p[P_DEC] = p->env[1];
        t->p[P_SUS] = p->env[2];
        t->p[P_REL] = p->env[3];
        t->p[P_ED_FLT] = p->fenv;
    }
    fm6_track_loaded(t);
    t->p[P_VOICE] = 1;
    t->p[P_RESERVED34] = 24;                     /* the sends, as melodee_init */
    t->p[P_RESERVED35] = 28;
    t->p[P_RESERVED36] = 36;
    song.master_q12 = 4096;
    and_out_init();                              /* the tracks' outputs, tracks 5..8's start (and_out.c) */
    and_texture_init();                          /* the texture engine (and_texture.c) */
    daisy_frame = 0;
    daisy_demo = 1;
}

/* one block, as web/andamento/sim.c's web_ms: the mix (every track, its notes out of MIDI and CV too: and_out.c), the
 * master bus, the texture engine */
void tessera_core_block(int32_t *out)
{
    if (daisy_demo) {
        uint32_t f = daisy_frame % (2u * FS), n = 0;  /* tests/hostsim.c's 2 s figure: C4, E4, G4 */
        if (f > FS / 10 && f < FS / 2)
            n |= 1u << 7;
        if (f > FS * 45 / 100 && f < FS * 8 / 10)
            n |= 1u << 11;
        if (f > FS * 9 / 10 && f < FS * 12 / 10)
            n |= 1u << 14;
        fm1_in.notes = n;
    }
    mix_block(out, CTL);
    and_master_block(out, CTL);
    and_texture_block(out, CTL);
    daisy_frame += CTL;
}

void tessera_core_tick_ms(void)
{
    fm1_ms++;
}

_Static_assert(TESSERA_SCREEN_BYTES == SSD_FRAME, "tessera_core.h: a frame of the SSD1322");

void tessera_core_screen_init(void)
{
    ssd1322_init();
}

/* the panel's inputs: from the main loop, so with the audio interrupt held off around each (and_ui.c changes the
 * song and the tracks, which the callback reads; each call is short). The first one ends the figure */
static void daisy_input(void)
{
    if (daisy_demo) {
        daisy_demo = 0;
        fm1_in.notes = 0;
    }
}
void tessera_core_enc(uint32_t i, int32_t n) { daisy_irq_off(); daisy_input(); and_enc(i, n); daisy_irq_on(); }
void tessera_core_enc_push(uint32_t i) { daisy_irq_off(); daisy_input(); and_enc_push(i); daisy_irq_on(); }
void tessera_core_data(int32_t n) { daisy_irq_off(); daisy_input(); and_data(n); daisy_irq_on(); }
void tessera_core_data_push(uint32_t down) { daisy_irq_off(); daisy_input(); and_data_push(down); daisy_irq_on(); }
void tessera_core_button(uint32_t b, uint32_t down) { daisy_irq_off(); daisy_input(); and_button(b, down); daisy_irq_on(); }
void tessera_core_key(uint32_t k, uint32_t down) { daisy_irq_off(); daisy_input(); and_key(k, down); daisy_irq_on(); }
void tessera_core_volume(uint32_t v)            /* as web/andamento/sim.c's web_volume: 0..1023, a square law */
{
    uint32_t k10 = v > 1023u ? 1023u : v;
    song.master_q12 = (int32_t)((k10 * k10) >> 8);
}

/* the screens: and_draw draws both frames; then each is packed and sent in turn (main.cpp selects the panel) */
void tessera_core_draw(void)
{
    and_draw();
}
void tessera_core_screen(uint8_t *buf, uint32_t screen)
{
    static uint8_t on[AND_NSCR];
    screen = screen < AND_NSCR ? screen : 0u;
    ssd1322_pack(and_fbs[screen], buf);
    ssd1322_frame(buf);
    if (!on[screen])
        ssd1322_on(on[screen] = 1);
}
void tessera_core_testcard(uint8_t *buf)        /* the bring-up card (and_testcard) on the panel selected */
{
    uint8_t (*was)[AND_W] = and_fb;
    and_fb = and_fbs[AND_SCR_PAGE];
    and_testcard();
    and_fb = was;
    ssd1322_pack(and_fbs[AND_SCR_PAGE], buf);
    ssd1322_frame(buf);
    ssd1322_on(1);
}

/* the lights and the tracks' outputs (and_out.c), for main.cpp to send on */
uint32_t tessera_core_led_buttons(void) { return and_led_buttons(); }
const uint8_t *tessera_core_led_keys(void) { return and_led_keys(); }
uint32_t tessera_core_midi_take(uint8_t *out, uint32_t max) { return and_out_midi_take(out, max); }
int32_t tessera_core_cv_mv(uint32_t pair) { return and_out_cv(pair); }
uint32_t tessera_core_gate(uint32_t pair) { return (uint32_t)and_out_gate(pair); }
