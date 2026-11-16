# CLAUDE.md

Andamento firmware: Theo Dainis's generative instrument on a Daisy Seed (GPL-3.0-only). Forked from Tessera
(github.com/Rastapopolix/tessera, the FM-1 firmware) on 2026-11-14 at Tessera `develop` 81aa730f, so Andamento can
grow past the FM-1's limits. Build flags and some file names still say `MELODEE_` / `tessera_`; rename them only as
part of a change that needs it.

Plans, decisions and handovers live in the private notes repo `tessera-notes`
(github.com/Rastapopolix/tessera-notes), folder `andamento/`: start with `andamento/README.md` (dated log, newest
first) and `andamento/FORK_PLAN.md`. Hardware design and decisions are in the private repo
github.com/Rastapopolix/andamento. Claim work under tessera-notes `ROADMAP.md` "In progress" with a line starting
**Andamento:** before touching code, and log what you did in `andamento/README.md` at the end. Push both repos.

## Working with Theo

- A modular-synth musician learning C/C++ and circuit design: explain the why in plain language, and git and shell
  steps plainly. Theo works from WSL Ubuntu on Windows (checkouts under `~/src`) and from cloud threads.
- One task per thread. GitHub is the one source of truth: never leave work only in a local folder.

## Branches

`main` is the trunk: feature branches from `main` (`claude/<topic>`), pull requests into `main`. No release
process yet.

## Relationship to Tessera (decided 2026-11-14)

- Andamento is its own firmware. It does not follow Tessera's parameter IDs, cord codes, project format or the
  FM-1's limits (flash, RAM, 4 tracks, 8 voices, 44.1 kHz). Changes here never need to keep the FM-1 working.
- **Its own project format.** Tessera `.tsp` files are not Andamento projects; an importer from `.tsp` may come
  later. Number claims (IDs, save parts) are Andamento's own, kept in `andamento/` in the notes repo, not in
  Tessera's ROADMAP "Number claims".
- Borrow from Tessera by hand when it helps (a new generator, a fix): pick the change, adapt it, credit it in the
  commit message (`From Tessera <sha> / PR #n`). Never merge Tessera wholesale.
- Generator, page and parameter names stay Tessera's where the idea is the same (shorten for the screen, don't
  rename the idea). On-screen names avoid other makers' product names (ATLAS, TIDE, SWARM, Beads); credits name the
  originals.

## Build and test

```
make -C platform/daisy LIBDAISY_DIR=/path/to/libDaisy   # the firmware; prints the memory report
web/andamento/build.sh                                  # the browser prototype (needs Emscripten / emcc)
web/andamento/build.sh --headers                        # only the generated headers (the host tests need them)
```

Host tests: `tests/and_tests.sh` (about a minute): Andamento's own jobs (`oled_render and_keys and_home and_ext
and_ui and_pics and_master and_texture and_macro and_project`) and the core's tests at 48 kHz, among them the golden renders against
`tests/golden_48k.txt`. No FM-1 package or JieLi SDK. `tests/and_tests.sh --list` names the jobs; name some to run only
those; `GOLDEN_UPDATE=1 tests/and_tests.sh regress` only after an intended change of the sound (review the diff).
Its header lists the core tests not yet ported to 48 kHz. `tests/run_tests.sh` is Tessera's runner (FM-1 jobs) and
goes with the FM-1 trim.

CI on every pull request and push to `main`: `platform/daisy` builds (`.github/workflows/daisy-build.yml`) and
`tests/and_tests.sh` passes (`.github/workflows/host-tests.yml`, with the screens' PNGs as an artifact).

## Hardware

- Daisy Seed (STM32H750, 480 MHz Cortex-M7, 64 KiB ITCM, 128 KiB DTCM, 512 KiB SRAM, 64 MiB SDRAM, QSPI flash).
  Audio at 48 kHz (`FS`). The layout and its checks: `platform/daisy/` (`tessera_daisy.lds`, `itcm_pick.py`,
  `memcheck.py`).
- Two SSD1322 128×64 OLEDs (16 grey levels), 8 encoders + ENERGY + VOL, 16 RGB keys, MIDI in/out, CV / gate for
  tracks 5 to 8. Details in the andamento repo.

## Rules

- Follow the existing conventions: the unity build (`web/andamento/sim.c`, `platform/daisy/` and the tests
  `#include` the `.c` files), C style, naming, comment density.
- All hardware access stays behind the platform layer, so the core and UI compile for host tests and the browser.
- Every automatic layer (Energy, Entries, Wander, Evolve, Gravity, the piece planner) has an off/zero position with
  no effect. Automation is opt-in.
- Every parameter has a central ID, range and help text. Fixed maximums for cords, sources, scopes, voices.
- Run the tests after every change. Report failures; do not hide them.
- Small commits, one change each, on a feature branch.
- Keep GPL-3.0 headers and the upstream attribution (Tessera, Melodee, Felucca, fm1-base), and a LICENSING.md row
  for each port.
- Do not commit build output, `__pycache__/` or backup files (`*.bak*`, `*.pre*`).
- Much of the tree is still the FM-1's (the JieLi HAL and build, the FM-1 UI, installer, recovery tools, site,
  Tessera's docs and releases). Remove or rewrite it in its own commits as Andamento replaces it; check the Daisy
  build and Andamento's tests still pass after each.
