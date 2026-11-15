/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Theo Dainis */
/* Andamento: what main.cpp (C++, libDaisy) calls in tessera_core.c (C, Tessera's core) */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define TESSERA_CORE_BLOCK 32           /* frames a call to tessera_core_block renders: Tessera's CTL */

void tessera_core_init(void);           /* tables, track 1 on FM6, tracks 5..8, the texture engine; before the audio */
void tessera_core_block(int32_t *out);  /* TESSERA_CORE_BLOCK stereo frames, interleaved L R, Q15: the mix, the master
                                         * bus, the texture engine; tracks 5..8 step on */
void tessera_core_tick_ms(void);        /* once a millisecond: Tessera's clock (fm1_ms) */

/* the panel (platform/andamento/and_ui.c): from the main loop, never the audio callback. Encoders 0..7 the grid, 8
 * ENERGY; buttons and_ui.c's AB_*; keys 0..15 (bottom row 0..7); VOL 0..1023. The first input ends the start-up
 * figure */
void tessera_core_enc(uint32_t i, int32_t n);          /* n detents, + clockwise */
void tessera_core_enc_push(uint32_t i);
void tessera_core_data(int32_t n);                     /* DATA, beside the page screen */
void tessera_core_data_push(uint32_t down);            /* 1 pushed, 0 let go */
void tessera_core_button(uint32_t b, uint32_t down);
void tessera_core_key(uint32_t k, uint32_t down);
void tessera_core_volume(uint32_t v);

#define TESSERA_SCREEN_BYTES 8192       /* a frame of the 128 x 64 screen as the SSD1322 takes it */
#define TESSERA_SCREEN_PAGE 0           /* the page screen (title, picture, tabs) */
#define TESSERA_SCREEN_DIAL 1           /* the dial screen, over the encoders */
void tessera_core_screen_init(void);    /* the SSD1322 set up, display off: after its reset pulse */
void tessera_core_draw(void);           /* both screens drawn (about 30 times a second; at least every 1.5 s) */
void tessera_core_screen(uint8_t *buf, uint32_t screen); /* one drawn screen packed into buf and sent; on at the first */
void tessera_core_testcard(uint8_t *buf); /* the bring-up card (and_testcard), packed into buf and sent */

/* the lights, and tracks 5..8's outputs (pair 0..3 = tracks 5..8), for the board to send on */
uint32_t tessera_core_led_buttons(void);               /* bit per AB_* button */
const uint8_t *tessera_core_led_keys(void);            /* 16 x RGB, 0..255 a channel, bottom row first */
uint32_t tessera_core_midi_take(uint8_t *out, uint32_t max); /* up to max 3-byte messages; how many */
int32_t tessera_core_cv_mv(uint32_t pair);             /* CV in mV, 1 V/oct, 0 V = MIDI 36 (assumed, see and_ext.c) */
uint32_t tessera_core_gate(uint32_t pair);             /* 1 while its note sounds */

/* main.cpp provides these to the screen driver (platform/andamento/ssd1322.c): its SPI, DC low / DC high, to the
 * panel selected (main.cpp's chip-selects) */
void and_spi_cmd(uint8_t c);
void and_spi_data(const uint8_t *p, uint32_t n);

#ifdef __cplusplus
}
#endif
