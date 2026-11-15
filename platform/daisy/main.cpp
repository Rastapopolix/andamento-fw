// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Theo Dainis
// Andamento: Tessera on a Daisy Seed. libDaisy starts the board and the codec at 48 kHz in blocks of 32 frames,
// Tessera's control block, and each audio callback asks tessera_core.c for one block (the mix, the master bus, the
// texture engine, tracks 5..8). The main loop keeps Tessera's millisecond clock, scans the panel, sends tracks
// 5..8's MIDI and CV / gates, draws the two screens about 30 times a second, prints the audio load over USB once a
// second, and blinks the Seed's LED, so a board that runs but stays silent says so.
//
// What is left for the board, each marked "the board:" below: the screens' SPI and chip-selects, the panel scan
// (74HC165s), MIDI out on the UART and USB, the CV DAC and the gate pins. Until then each does nothing, and the
// build checks that everything else compiles and fits.
// Where things live (tessera_daisy.lds): the audio path's code in ITCM, its tables and state in DTCM, the rest
// of the data in the AXI SRAM, DMA buffers in RAM_D2_DMA, long buffers (later) in the SDRAM.
#include "daisy_seed.h"
#include "tessera_core.h"

using namespace daisy;

static DaisySeed hw;
static CpuLoadMeter load;               // the audio callback's share of the time: the first number to measure
// Its peak over the last second, 0..1, where a debugger (SWD) can watch it; also printed over USB once a second
// (load_report). Above about 0.8, a voice or an effect too many will soon click
volatile float tessera_core_load;

// The screen: the yellow NHD-2.7-12864WDY3-M (SSD1322) in 4-wire SPI. tessera_core.c draws and packs a frame; these
// two calls are all it needs from the board. Its packed frame lives in memory the SPI's DMA can read (not cached).
// Left for the board (DAISY_PORT.md task 5): SPI1 with SCK on D8 (pin 9), MOSI on D10 (pin 11), CS on D7 (pin 8),
// at most 10 MHz, mode 0; DC and RESET on two free GPIO, chosen when it is wired. RESET low for at least 2 us, then
// a wait before tessera_core_screen_init. Until then the calls do nothing, and the build checks the rest compiles.
// Two screens share the bus, each with its own chip-select (screen_select): one buffer serves both, sent in turn,
// so with DMA the second frame waits for the first to finish (RAM_D2_DMA has room for one, not two).
static uint8_t DMA_BUFFER_MEM_SECTION screen_buf[TESSERA_SCREEN_BYTES];

static void screen_select(uint32_t screen)
{
    (void)screen;                        // the board: the page screen's CS low (D7), or the dial screen's (a free GPIO)
}

extern "C" void and_spi_cmd(uint8_t c)
{
    (void)c;                             // the board: DC low, the byte out on SPI1
}

extern "C" void and_spi_data(const uint8_t *p, uint32_t n)
{
    (void)p;                             // the board: DC high, the bytes out on SPI1 (a frame by DMA)
    (void)n;
}

// The fast memories, filled before anything uses them: libDaisy's startup copies .data and zeroes .bss, and
// nothing else. The symbols come from tessera_daisy.lds.
extern "C" uint32_t _sitcm_text[], _eitcm_text[], _litcm_text[];
extern "C" uint32_t _sdtcm_data[], _edtcm_data[], _ldtcm_data[];
extern "C" uint32_t _sdtcm_bss[], _edtcm_bss[];
extern "C" uint32_t _ssdram_bss[], _esdram_bss[];

static void tessera_memory_init(void)
{
    // volatile: plain word copies, which the compiler can't turn into a memcpy (that one runs from QSPI, fine,
    // but this way the copy needs nothing that isn't there yet)
    volatile uint32_t *d = _sitcm_text;
    for (const uint32_t *s = _litcm_text; d < _eitcm_text;)
        *d++ = *s++;
    d = _sdtcm_data;
    for (const uint32_t *s = _ldtcm_data; d < _edtcm_data;)
        *d++ = *s++;
    for (d = _sdtcm_bss; d < _edtcm_bss;)
        *d++ = 0;
    __DSB();                             // the copies done before the first instruction is fetched from ITCM
    __ISB();
}

// The panel, scanned once a millisecond (DAISY_PORT.md wiring): the keys and buttons in a matrix, the 10 encoders
// (8 + ENERGY + DATA: A, B and push each) through four 74HC165s, VOL on an ADC pin. Each change goes to tessera_core's
// calls: an encoder's detents as tessera_core_enc(i, n), a push, a button or key down and up.
static void panel_scan(void)
{
    // the board: shift the 74HC165s in (PL low then high, 32 clocks), decode each encoder's A/B into detents,
    // debounce the pushes, scan the key matrix, read VOL; then tessera_core_enc / _enc_push / _data / _data_push /
    // _button / _key / _volume for what changed
}

// Tracks 5..8 out, once a millisecond: their MIDI (TRS on a UART, and USB MIDI), CV on the 16-bit DAC, gates on 4
// GPIO. The ring holds 3-byte messages; the CV is in mV for the DAC's scale and offset (calibrated per unit).
static void outputs_send(void)
{
    uint8_t midi[16 * 3];
    uint32_t n = tessera_core_midi_take(midi, 16);
    (void)n;                             // the board: the n messages out on the MIDI UART (31,250 baud) and USB MIDI
    for (uint32_t pair = 0; pair < 4; pair++) {
        (void)tessera_core_cv_mv(pair);  // the board: to the DAC (DAC8564), scaled and offset by its calibration
        (void)tessera_core_gate(pair);   // the board: to the gate pin
    }
}

// Once a second over USB (a serial terminal on the PC: PuTTY, or `screen /dev/ttyACM0` in Linux): the audio
// callback's load, average and peak, in percent. Starts the peak over for the next second.
static void load_report(void)
{
    float avg = load.GetAvgCpuLoad(), peak = load.GetMaxCpuLoad();
    tessera_core_load = peak;
    hw.PrintLine("load avg %d%% peak %d%%", (int)(avg * 100.0f + 0.5f), (int)(peak * 100.0f + 0.5f));
    load.Reset();
}

static void sdram_clear(void)            // after hw.Init, which starts the SDRAM: DSY_SDRAM_BSS starts at zero
{
    for (volatile uint32_t *d = _ssdram_bss; d < _esdram_bss;)
        *d++ = 0;
}

// The audio callback: libDaisy's DMA interrupt, every 32 frames (0.67 ms at 48 kHz). It runs from QSPI itself (it
// is short), and calls into ITCM for the rest. The block is on the stack, in DTCM.
static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    (void)in;
    load.OnBlockStart();
    int32_t q15[2 * TESSERA_CORE_BLOCK]; // (mix_block clips to Q15 already: the float stays in -1..1)
    for (size_t i = 0; i < size; i += TESSERA_CORE_BLOCK) {
        tessera_core_block(q15);
        for (size_t k = 0; k < TESSERA_CORE_BLOCK && i + k < size; k++) {
            out[0][i + k] = (float)q15[2 * k] * (1.0f / 32768.0f);       // Q15 -> -1..1
            out[1][i + k] = (float)q15[2 * k + 1] * (1.0f / 32768.0f);
        }
    }
    load.OnBlockEnd();
}

int main(void)
{
    tessera_memory_init();               // first: ITCM and DTCM, before any of Tessera's code runs
    hw.Init(true);                       // true: the CPU at its full 480 MHz
    sdram_clear();                       // (the texture engine's capture lives there: and_texture.c)
    hw.StartLog(false);                  // USB serial for load_report; false: don't wait for a terminal
    hw.SetAudioBlockSize(TESSERA_CORE_BLOCK);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    load.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
    tessera_core_init();
    tessera_core_screen_init();
    screen_select(TESSERA_SCREEN_PAGE);
    tessera_core_testcard(screen_buf);   // the bring-up card for a second: the panel's offset and orientation
    hw.StartAudio(AudioCallback);

    uint32_t last = System::GetNow(), drawn = last, reported = last, start = last;
    for (;;) {
        uint32_t now = System::GetNow();
        while (last != now) {            // Tessera's clock, one tick a millisecond, and what runs with it
            tessera_core_tick_ms();
            panel_scan();
            outputs_send();
            last++;
        }
        if (now - start >= 1000u && now - drawn >= 33u) {   // the screens about 30 times a second, after the card
            drawn = now;
            tessera_core_draw();
            for (uint32_t s = TESSERA_SCREEN_PAGE; s <= TESSERA_SCREEN_DIAL; s++) {
                screen_select(s);
                tessera_core_screen(screen_buf, s);
            }
        }
        if (now - reported >= 1000u) {
            reported = now;
            load_report();
        }
        hw.SetLed((now / 500u) & 1u);    // alive: a slow blink
    }
}
