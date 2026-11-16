# Andamento firmware

Firmware for **Andamento**, Theo Dainis's generative instrument on a Daisy Seed: eight tracks, two 128×64 OLEDs
over eight encoders, sixteen RGB keys, MIDI and CV / gate out.

Andamento started as a port of [Tessera](https://github.com/Rastapopolix/tessera), the generative firmware for the
M-Vave FM-1, and forked from it on 2026-11-14 (Tessera `develop` 81aa730f) so it can grow past the FM-1's limits:
more tracks, more voices, its own project format. It keeps Tessera's generators, sequencer, engines and mod system
as its starting point and borrows from Tessera by hand when that helps; the two no longer share IDs or project
files.

GPL-3.0-only. **Status: in development, no hardware yet.** By **Theo Dainis**
([Rastapopolix](https://github.com/Rastapopolix)).

## Where things are

| Path | What |
| --- | --- |
| `platform/daisy/` | the Daisy Seed build (libDaisy, 48 kHz audio path, memory placement) |
| `platform/andamento/` | Andamento's own: the two screens, the page map, the keys, tracks 5 to 8, master bus, texture engine |
| `web/andamento/` | the virtual prototype: the whole instrument in a browser (`web/andamento/build.sh`, needs Emscripten) |
| `firmware/src/` | the core inherited from Tessera: generators, sequencer, engines, mods |
| `tests/` | host tests: `tests/and_tests.sh` runs Andamento's (`tests/and_*_test.c`, `tests/oled_render.c`) and the core's at 48 kHz |

Much of the tree is still Tessera's FM-1 code (its HAL, UI, installer, site and docs) and is being removed or
rewritten as Andamento needs: see `CLAUDE.md`. Tessera's own README is kept as `README-tessera.md` until then.

## Build

```
make -C platform/daisy LIBDAISY_DIR=/path/to/libDaisy   # the firmware (arm-none-eabi)
web/andamento/build.sh                                  # the browser prototype (Emscripten)
tests/and_tests.sh                                      # the host tests (a C compiler, Python 3, Pillow)
```

## Credits

Andamento builds on Tessera by Theo Dainis, which grew out of [Melodee](https://github.com/keremimo/melodee) by
**keremimo** (Kerem Kilic, Ellic Studio), itself a modified version of
[Felucca](https://github.com/hugelton/Felucca) by **Leo Kuroshita** (@kurogedelic), Hügelton Instruments. The
licences of every part, and the ports from other projects, are in [LICENSING.md](LICENSING.md).
