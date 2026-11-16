#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Andamento's host tests: the PC builds of Andamento's own code (platform/andamento) and of the shared core, all at the
# Daisy Seed's 48 kHz. No FM-1 package, JieLi toolchain or AC79 SDK is needed (tests/run_tests.sh, Tessera's runner,
# still wants them until the FM-1 is trimmed out of this tree). Run from anywhere:
#   tests/and_tests.sh                every job (the gate before a pull request; CI runs this)
#   tests/and_tests.sh regress and_ui only the jobs named
#   tests/and_tests.sh -v             every job's whole output (default: a line per job, a failure's own output)
#   tests/and_tests.sh --list         the job names
#   GOLDEN_UPDATE=1 tests/and_tests.sh regress   rewrites tests/golden_48k.txt: only after an intended change of sound
# Needs a C compiler (CC, default cc), Python 3 and Pillow (the generated headers; the screens' PNGs).
#
# ANDAMENTO (48 kHz, the virtual prototype's build flags, as web/andamento/build.sh):
#   oled_render   the SSD1322 driver through a model of the controller, each frame read back (tests/oled_render.c)
#   and_keys      the 16 keys and their RGB lights           and_home     HOME, DEPTH, the 8 lanes, ENERGY's arc
#   and_out       8 tracks: SOUND, MIDI, CV / gate           and_ui     both screens on every page, their layout
#   and_pics      the generators' pictures on GEN            and_master   the master bus (tilt EQ, compressor)
#   and_texture   the texture engine                     and_macro    the MACRO engine (Plaits, C++: needs c++)
#   and_project   the project file: round trip, unknowns skipped, refusals, the golden tests/fixtures/andp_v1.and
#   and_voice     the shared voice stage (firmware/src/vstage.c): filter types, CUT, RES, ENV, KEY, DRIVE, VEL
#   The screens' PNGs go to build/oled (tests/oled_render.py).
# CORE at 48 kHz (firmware/src with the tables made at MELODEE_FS=48000, as platform/daisy/Makefile makes them):
#   regress       golden renders (every engine x preset, voice modes, FX, a 4-track mix) against tests/golden_48k.txt,
#                 health (clipping, DC, level, voices freed, silence), voices and routing. (Its CPU budget counts
#                 instructions on macOS only; elsewhere it is not checked.)
#   fs_test       every engine in tune at 48 kHz as at 44.1, the 808 metal and PHYS decays
#   gen_test chain_test entries_test planner_test split_test recipes_test: the generators and the long-form layers
#   pid_test cord_test help_pack_test tables_test: parameter ids, cords, help texts, readouts
#   mod_test mod_spread_test fm6_test drum_test chord_test: modulation, FM6, the 808, chord keys
# Not here yet (their checks still assume 44.1 kHz in numbers or a baseline; tests/run_tests.sh runs them at 44.1):
#   tessera_test (tests/tessera_baseline.txt is 44.1 kHz's), follow_test, cordclock_test, noise_test, fx_test
#   (reverb rate conversion: 11025 Hz is FS / 4 only at 44.1).
set -u
cd "$(dirname "$0")/.."
OUT=build/andamento/host
LOG=$OUT/log
GEN=build/andamento/gen
CC="${CC:-cc} -ffp-contract=off -Wall -Wno-unused-function"
CORE="-w -I$GEN -Ifirmware/src -Itests"
AND="$CORE -DMELODEE_USB_AUDIO=1 -DMELODEE_ACCESS=0 -DMELODEE_LOOK=1 -DMELODEE_FM6=1 -DMELODEE_PHYS=1"
PARALLEL=${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
QUIET=1
PIL=0; python3 -c "import PIL" 2>/dev/null && PIL=1

JOBS="oled_render and_keys and_home and_out and_ui and_pics and_master and_texture and_macro and_project and_voice
    regress fs_test gen_test chain_test entries_test planner_test split_test recipes_test
    pid_test cord_test help_pack_test tables_test mod_test mod_spread_test fm6_test drum_test chord_test"
WANT=
for a; do
    case $a in
        -v | --verbose) QUIET=0 ;;
        --list) echo $JOBS; exit 0 ;;
        -*) echo "usage: tests/and_tests.sh [-v] [--list] [JOB...]"; exit 2 ;;
        *) case " $(echo $JOBS) " in *" $a "*) WANT="$WANT $a" ;; *) echo "no job $a (--list)"; exit 2 ;; esac ;;
    esac
done
WANT=${WANT:-$JOBS}

# ------------------------------------------------------------------ setup ---
# the generated headers once, before the jobs (each used to make them itself, racing the others): build/gen at
# 44.1 kHz (fs_test compares against it) and build/andamento/gen at 48 (web/andamento/build.sh --headers)
mkdir -p "$OUT" "$LOG"
rm -f "$LOG"/*
start=$(date +%s)
python3 tools/gen_scales.py --check || exit 1
sh web/andamento/build.sh --headers > "$LOG/headers.txt" 2>&1 || { cat "$LOG/headers.txt"; echo "headers FAILED"; exit 1; }
[ -f build/gen/x0x_drum_tables.h ] || python3 tools/gen_909_tables.py build/gen/x0x_drum_tables.h > /dev/null || exit 1

# ------------------------------------------------------------------- jobs ---
# In a job: build OUT SRC FLAGS... (tests/SRC.c -> $OUT/OUT), then the commands; a failure is a line in $LOG/<job>.fail.
fail() { echo "$1" >> "$LOG/$J.fail"; }
build() {
    local out=$1 src=$2; shift 2
    $CC "$@" -o "$OUT/$out" "tests/$src.c" -lm -pthread || { fail "compile tests/$src.c"; return 1; }
}
pngs() { [ $PIL = 0 ] || python3 tests/oled_render.py build/oled > /dev/null || fail "tests/oled_render.py"; }
# t TITLE FLAGS... [-- ARGS...]: tests/$J.c built with FLAGS and run with ARGS
t() {
    local title=$1 flags=; shift
    while [ $# -gt 0 ] && [ "$1" != -- ]; do flags="$flags $1"; shift; done
    [ $# -gt 0 ] && shift
    echo "== $title"
    build "$J" "$J" $flags || return
    "$OUT/$J" "$@" || fail "$title"
}
job_oled_render() {
    mkdir -p build/oled/ppm
    t "Andamento screen (platform/andamento): the SSD1322 driver through a model of the controller, the frame read back" \
        -O1 -- build/oled && pngs
}
job_and_keys() { echo "== Andamento keys: SCALE, CHROM, STEP, MUTE, their RGB lights, ENERGY, activity"
    build and_keys and_keys_test -O1 $AND && { "$OUT/and_keys" || fail "and_keys"; }; }
job_and_home() { J0=and_home_test screen_of "Andamento home: HOME and NO, DEPTH, the 8 lanes, ENERGY's arc FREE and planned, a rest"; }
job_and_out() { J0=and_out_test screen_of "Andamento outputs: 8 tracks, SOUND, MIDI CH, CV / gate, STOP, MUTE, keys, MIX OUT, 1..4 unchanged"; }
job_and_ui() { J0=and_ui_test screen_of "Andamento screens: the page and dial screens on every page, their layout, a setting turned"; }
job_and_pics() { J0=and_pics_test screen_of "Andamento pictures: every generator's on GEN, drawn and inside its rows"; }
screen_of() {                                     # an and_*_test.c whose binary is the job's name
    mkdir -p build/oled/ppm
    echo "== $1"
    build "$J" "$J0" -O1 $AND && { "$OUT/$J" build/oled || fail "$1"; } && pngs
}
job_and_voice() { echo "== Andamento voice stage (vstage.c): FILTER LP LP4 BP HP, CUT, RES, ENV, KEY, DRIVE, VEL, every engine, the project file"
    build and_voice and_voice_test -O1 $AND && { "$OUT/and_voice" || fail "and_voice"; }; }
job_and_master() { echo "== Andamento master bus: MIX 0 bit-identical, bypass, compressor, release, makeup, tilt, determinism"
    build and_master and_master_test -O1 $AND && { "$OUT/and_master" || fail "and_master"; }; }
job_and_texture() { echo "== Andamento texture engine: MIX 0 bit-identical, capture, FREEZE, DENS, AT_GRAINS bound, determinism"
    build and_texture and_texture_test -O1 $AND && { "$OUT/and_texture" || fail "and_texture"; }; }
job_and_project() { echo "== Andamento project file: round trip, unknowns skipped, defaults, clamped, refusals, golden file"
    build and_project and_project_test -O1 $AND && { "$OUT/and_project" tests/fixtures/andp_v1.and || fail "and_project"; }; }
job_and_macro() { echo "== Andamento MACRO (eng_macro.c, Plaits): every model plays, pitch at 48 kHz, HIT, every knob, arena memory, cost"
    command -v c++ > /dev/null || { fail "no c++ (Plaits is C++)"; return; }
    local f o objs= flags; flags=$(sh platform/daisy/plaits_sources.sh --flags)
    mkdir -p "$OUT/mi"
    for f in $(sh platform/daisy/plaits_sources.sh); do      # (Plaits as platform/daisy builds it, for this computer)
        o=$OUT/mi/$(echo "$f" | tr / _).o
        c++ -O2 -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics -w $flags -c "$f" -o "$o" ||
            { fail "compile $f"; return; }
        objs="$objs $o"
    done
    $CC -O2 $AND -DMELODEE_PLAITS=1 -c tests/macro_test.c -o "$OUT/macro_test.o" &&
        c++ -o "$OUT/and_macro" "$OUT/macro_test.o" $objs -lm -pthread || { fail "compile tests/macro_test.c"; return; }
    "$OUT/and_macro" || fail "and_macro"
}

job_regress() {
    t "regression at 48 kHz: golden renders (tests/golden_48k.txt), health, voices, routing" -O2 $CORE -- \
        tests/golden_48k.txt "$OUT/cpu_48k.txt"
}
job_fs_test() { echo "== sample rate: every engine in tune at 48 kHz as at 44.1 (FS, tools/gen_tables.py)"
    python3 tests/fs_test.py "$OUT/fs" || fail "tests/fs_test.py"; }
job_gen_test() { t "GEN: each generator on its own: ranges, rests, draws per step, determinism, mutation, sections" -O2 $CORE; }
job_chain_test() { t "FORM CHAIN: order, counts, FOLLOW / OWN, SKIP, WANDER, OFF unchanged" -O2 $CORE; }
job_entries_test() { t "ENTRIES: tracks out and back on the phrase lines, DWELL, anchors, breakdowns, CUT / FADE, OFF unchanged" -O2 $CORE; }
job_planner_test() { t "PIECE: the planner's SHAPEs, FREE unchanged, ENERGY / Entries / the CHAIN steered, END, tempo" -O2 $CORE; }
job_split_test() { t "SPLIT: TIMING's SPLIT on generators, STEP and drums; SPLIT 0 unchanged" -O1 $CORE; }
job_recipes_test() { t "RECIPES: GENERATIVE.md's Krell and Long gates played" -O2 $CORE; }
job_pid_test() { t "PARAMETER IDS: every setting numbered, ranges, get/set, frozen against tests/pid_ids.txt" -O1 $CORE -- \
    tests/pid_ids.txt docs/PARAMETERS.md; }
job_cord_test() { t "CORDS: the destination table (tests/cord_dests.txt), LAG, DRIFT, buses" -O1 $CORE -- tests/cord_dests.txt; }
job_help_pack_test() { t "HELP: the packed help texts expand to pid.c's" -O1 $CORE; }
job_tables_test() { t "TABLES: the computed readouts (TIME, LFO Hz, CUTOFF, dB) equal the tables they replace" -O1 $CORE; }
job_mod_test() { mkdir -p "$OUT/mod_demo"
    t "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, demos" -DMELODEE_LEGACY_EXTRAS=1 -O2 $CORE -- \
        "$OUT/mod_demo"; }
job_mod_spread_test() { t "matrix S&H / SLEW / DEPTH, SPREAD: off = bit-identical, stereo sides and routing" -O2 $CORE; }
job_fm6_test() { mkdir -p "$OUT/fm6_demo"
    t "FM6: the DX7's algorithms, pitch, levels, envelopes, modulation, macros, patches, demos" -O2 $CORE -- "$OUT/fm6_demo"; }
job_drum_test() { mkdir -p "$OUT/drum_demo"
    t "DRUM: 808 instruments, velocity, hat choke, release, eight lanes, demos" -O2 $CORE -- "$OUT/drum_demo"; }
job_chord_test() { t "chord keys: diatonic and fixed chords, voicings, MONO root, releases, ARP, MIDI IN" -O1 $CORE; }

# ---------------------------------------------------------------- running ---
# each job in the background (PARALLEL at once), its output in $LOG/<job>.log; printed in order once all have ended
run_job() { J=$1; "job_$1" > "$LOG/$1.log" 2>&1; touch "$LOG/$1.done"; }
report() {
    local j=$1 l=$LOG/$1.log title
    title=$(sed -n '1s/^== //p' "$l")
    if [ ! -f "$LOG/$j.fail" ]; then
        if [ $QUIET = 1 ]; then printf 'ok    %-16s %.90s\n' "$j" "$title"; else cat "$l"; fi
        return
    fi
    printf 'FAIL  %-16s %s\n' "$j" "$title"
    if [ $QUIET = 0 ] || [ "$(wc -l < "$l")" -le 60 ]; then
        sed 's/^/  | /' "$l"
    else
        grep -n -i -E 'fail|error|changed|crash|assert|differ|mismatch' "$l" | head -25 | sed 's/^/  | line /'
        echo "  | ... the last 30 of $(wc -l < "$l") lines:"
        tail -n 30 "$l" | sed 's/^/  | /'
    fi
    sed 's/^/  failed: /' "$LOG/$j.fail"
    echo "  full log: $l"
    sed "s/^/$j: /" "$LOG/$j.fail" >> "$LOG/failed"
}
trap 'kill $(jobs -p) 2>/dev/null; exit 130' INT TERM
for j in $WANT; do
    while [ "$(jobs -rp | wc -l)" -ge "$PARALLEL" ]; do wait -n; done
    run_job "$j" &
done
wait
for j in $WANT; do report "$j"; done
secs=$(($(date +%s) - start))
n=$(echo $WANT | wc -w)
if [ -s "$LOG/failed" ]; then
    echo "ANDAMENTO HOST TESTS FAILED ($n jobs, $secs s):"
    sed 's/^/  /' "$LOG/failed"
    exit 1
fi
echo "ANDAMENTO HOST TESTS PASSED ($n jobs, $secs s)"
