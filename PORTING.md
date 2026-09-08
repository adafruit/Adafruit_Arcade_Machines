<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Porting a new machine

This is the distilled method for adding an arcade machine to this library —
the order of work that has now produced seven ports, and the traps that
actually cost time. It is deliberately short. `DEVNOTES.md` is the full
record and this file cites it by number; read the cited entry before
arguing with a rule here, because every one of them is a bug that shipped.

**One port reached working hardware with zero hardware debug cycles**
(Ms. Pac-Man). It did that by following the order below strictly. Skipping
straight to flashing has never been faster.

## What a port is

Three axes meet in every build, and a new machine only touches one of them:

| Axis | Lives in | You add |
|---|---|---|
| CPU core | `src/cpu/<cpu>/` | nothing, if your CPU is already there |
| Machine | `src/machines/<game>/` | **this is the port** |
| Board | `src/boards/<board>/` | nothing |
| Composition root | `examples/Games/<game>_fruitjam/` | a thin sketch |

Available CPU cores: `i8080`, `z80`, `m6502`, `mcs48`. A new CPU family is a
sibling directory under `src/cpu/`, with no hardware or game knowledge in
it at all — registers, memory, and per-instance callbacks, nothing else.

The architectural rule that makes the whole thing work: **a machine talks
only to the HAL contracts in `src/hal/`, never to anything under
`src/boards/`.** That is what lets the same machine code run natively on
your laptop, which is the single biggest accelerator available to you.

## The six modules

Every machine is the same six modules. Follow the naming; the uniformity is
what makes an unfamiliar game readable.

```
src/machines/<game>/
  <game>_machine.{h,cpp}   the <game>_system struct, init, load_assets, run_frame
  <game>_ports.{h,cpp}     memory map and I/O decode; wires the CPU callbacks
  <game>_video.{h,cpp}     per-scanline renderer + the boot error frame
  <game>_audio.{h,cpp}     the HAL audio fill callback
  <game>_input.{h,cpp}     board buttons -> this game's input bits
  <game>_assets.{h,cpp}    ROM/PROM manifest and any decode
```

The public surface a sketch uses is five functions and a struct:

```c
void <game>_init(<game>_system *system);           // state defaults + hal_video_init()
bool <game>_load_assets(<game>_system *system, uint16_t *out_error_color);
void <game>_input_update(<game>_system *system, /* buttons */ ...);
void <game>_run_frame(<game>_system *system);      // one frame, interleaved
void <game>_draw_error_frame(uint16_t color);      // keeps the DVI queue fed on failure
```

That last one matters more than it looks: when `load_assets` fails the
sketch must *still* feed scanlines every `loop()`, or the queue starves and
the failure renders as a glitching red instead of the solid diagnostic
colour you meant.

Each machine declares its own `<game>_system` type. (`invaders` and
`lrescue` share the name `arcade_system` for historical reasons — do not
copy that; name yours `<game>_system`.)

## The HAL contract

21 functions across `src/hal/arcade_hal_{video,audio,input,storage}.h`.
A machine uses only a handful of them:

- **video** — `hal_video_acquire_scanline()` / `hal_video_submit_scanline()`.
  The rest (`take_blocked_us`, `take_starve_count`, `take_min_valid_level`,
  `scanbuf_count`) are instrumentation the *sketch* prints, not machine API.
- **audio** — `hal_audio_init()`, `hal_audio_set_fill_callback()`, and the
  `enter_critical`/`exit_critical` pair around anything the fill callback
  also touches.
- **storage** — `mount`, `list_dir`, `open`, `read`, `close`, `unmount`.
  Storage is touched **once**, at boot; the game then runs entirely from RAM.

## Order of work

### 1. Verify the ROM set before writing a line of code

One loop over `zlib.crc32`, compared against MAME's published CRC32s for
that set. On Ms. Pac-Man this eliminated an entire branch of a live
investigation in one command. A bad dump presents exactly like a decode bug.

### 2. Read MAME's actual driver source, and cite it

Fetch and read it — do not work from recollection. Put the citation in the
file-header comment naming driver, function, and what you took from it.
Every existing machine does this, and it has repeatedly been the difference
between a fix and a guess. Facts worth citing: memory map, I/O map,
interrupt scheme, tile/sprite decode, palette decode, sound registers, DIP
layout, and the `ROT` flag (see *Rotation* below).

### 3. Build the host harness before flashing anything

```
tools/<game>_host/
  build.sh     copy a sibling's; point -I at src/, src/hal, src/cpu/<cpu>, src/machines/<game>
  main.cpp     argument parsing, scripted input, PPM/WAV dumping
```

`tools/host_common/` already provides the stub HAL and the
`<Arduino.h>`/`<pico.h>` shims. This compiles the **real** machine — real
CPU core, real ROMs, real port decode, real per-scanline interleave —
natively, so iteration goes from minutes per hardware cycle to under a
second, with frame rendering to PPM, audio to WAV, and scripted input.
On Galaga it reproduced a multi-session deadlock bit-for-bit on its first
run.

**Run a sibling harness as a control before believing any symptom.** A
garbled first frame looked like a broken decode until `pacman_host`, whose
game is hardware-confirmed, produced an equally garbled frame from the same
point — both were attract screens. Cost of that control: one command.

### 4. Then flash, and verify video, input, and audio separately

Add the example sketch under `examples/Games/`, with a `sketch.yaml` pinning
`opt=Optimize2` or `Optimize3`. See `README.md` for the flash-and-read-serial
loop; no debug probe is used or needed.

## The traps, ranked by time cost

- **Measure the quantity you care about, not a proxy** (#32). The worst
  single mistake in this project: "every press fires twice" was declared
  fixed because rendered frames were byte-identical across press lengths —
  all of them were firing two bullets. *A comparison among variants cannot
  detect a fault they all share.* Byte-identical frame comparison answers
  "did this change behaviour"; it never answers "is this correct."
- **A symptom that could plausibly be authentic still deserves a
  measurement.** Two bullets on screen *is* real Galaga. That is why the bug
  survived a whole session after being dismissed once.
- **Never run a long uninterrupted burst between scanline submissions**
  (#18, #20, #34, #36, #48 — five times). Interleave CPU execution with the
  acquire/submit pump. Core 1 can only coast on its buffered scanlines plus
  vblank, so the rule is *not* "is there budget left" but **"is there ever a
  gap longer than ~2ms between two submissions."** #48 is the sharpest case:
  a carefully interleaved frame with a 2.9ms *audio* burst bolted onto the
  end broke pacing outright while using well under half the budget.
- **`hal_video_acquire_scanline()` blocks.** Any whole-frame timing therefore
  reads `max(work, DVI period)` and pins at ~16.7ms, revealing nothing.
  Subtract `hal_video_take_blocked_us()` and look at `work`. #16 dead-ended
  for sessions for want of that one split.
- **Frame totals cannot see within-frame deficits** (#35). Per-scanline cost
  is very uneven; Core 0 loses ground on expensive rows and repays it on
  cheap ones, so `work` reads comfortable while the queue drained mid-frame.
- **Print a total and a count beside every maximum.** Three wrong
  conclusions here came from bare maxima.
- **`PORT_DIPUNUSED` in MAME does not mean "reads 0"** (#24). Unused DIP bits
  and open-bus reads are load-bearing — arcade ROMs self-reset when a
  protection check sees a constant where it expects noise.
- **For approximated audio, derive the pitch and measure the shape** (#46).
  Component values give clock rates and 555 frequencies exactly; they cannot
  give envelope lengths or whether a sweep ramps or warbles. Get recordings,
  and compare the whole contour — a matching *mean* pitch hid a wrong sound
  twice. Gameplay captures answer mechanism; isolated recordings answer
  timbre (#47).
- **Set audio levels from the device, not a host capture** (#63). A
  40-second host WAV peaked at 64.5% with nothing clipped; the device then
  reported 37004 against a 32767 ceiling. A capture is a sample of
  behaviour, not a bound on it.
- **The host harness cannot see flash stalls** (#60) — it has no XIP. On
  Burger Time it reported audio at 4.4% of the frame where the device
  measured 27%. Use the harness for *what the machine is doing*; use the
  device heartbeat for *where the time goes*.
- **No `printf`/`Serial` in any hot path, ever** (#16, #35). One left inside
  an audio ISR blocked on USB CDC exactly when a sound triggered.
- **But a silent failure path needs a designed diagnostic** (#43). A first
  flash with zero serial output is indistinguishable from a hang. A
  boot-time print is lost to the USB CDC attach race, so report failures
  from inside `loop()` once per second, and have the loader **name the files
  it could not open**.
- **Cast to the counter's exact width, never `long`** (#26, #27). `long` is
  32-bit on device and 64-bit on a host, so a harness cannot reproduce a
  wraparound bug unless the types match — and one hid there for a while.
- **Swap tests prove nothing if more than one variable moves** (#32).
  Swapping two buttons a human presses does not isolate the buttons.
- **When the real fix lands, remove the interim mitigation and re-measure**
  (#34). A workaround's cost outlives its purpose silently.
- **Expect a third of your optimisation guesses to measure zero** (#62). Two
  of six did on Burger Time. Measure after *every* one, or the worthless
  changes hide inside the wins.
- **This target has no hardware float** — `-mfloat-abi=softfp`, no `-mfpu`,
  so every float op is emulated and a divide costs ~120 cycles. Never
  compute filter coefficients per sample.

## Rotation

The default rotation is a **per-machine hardware fact** and cannot be copied
from a neighbouring game (#33, and #41 repeating it in the opposite
direction) — the 8080bw and Namco cabinets mounted their monitors opposite
ways.

- **Predictor:** MAME's `ROT` flag in the driver's `GAME()` line. `ROT270`
  → rotation 1, `ROT90` → rotation 3. Seven for seven across every game
  here (invaders, lrescue, dkong, btime = 1; pacman, mspacman, galaga = 3).
- **Measurement:** the framebuffer invariant — **the top of the game's
  picture must land on the right-hand side of the DVI framebuffer.** Render
  the candidate rotations in the harness and compare where the score text
  lands against a known-good frame from a confirmed game.

Read the flag to pick the starting value, then verify with the invariant.
The flag is a prediction; the render is the measurement. And check your own
viewing orientation before diagnosing a rendering bug from a photo — a 180°
error looks exactly like a flip bug.

`DISPLAY_GEOMETRY.md` has the derivation, per-game measurements, and the
frame-budget cost of each rotation and of aspect correction.

## Performance levers, in the order they paid off

Reach for these only against a measurement. Note which are still unapplied
— those are the cheap wins left in the tree.

1. **Move the CPU interpreter and hot paths to SRAM** (`__not_in_flash` /
   time-critical sections). Biggest single win on Galaga: 54–57fps decaying
   → flat 59. Done for `z80`, `m6502`, `mcs48`; **not done for `i8080`.**
2. **Decode sprites once per frame, not per scanline.**
3. **Interleave CPU with the scanline pump** — also a correctness fix, see
   the trap above.
4. **Per-rotation fast paths** that render straight into the caller's
   buffer. Any rotation that is a game default must have one (#33).
5. **Flattened pen LUTs** instead of dependent PROM/palette lookups.
6. **`opt=Optimize3`** per example. There is no project-wide right answer,
   which is why each `sketch.yaml` pins its own.
7. **Dispatch memory decode on the high address nibble**, not a chain of
   range tests. Burger Time had ROM — the target of every opcode and operand
   fetch — behind eight comparisons; `switch (addr >> 12)` was worth **3.2ms
   of a 16.66ms frame** (#62).
8. **A direct-read page table in the CPU core.** A memory callback is an
   *indirect* call: it cannot be inlined and costs more than the read it
   performs. `m6502` takes an optional `rd_page[256]` of pointers to pure
   memory pages (NULL for anything with a read side effect, a permuted
   window, or a partly-mapped page). Worth **2.1ms**, verified by a digest
   A/B over 1,200 frames. **The same trick applies to the other three cores
   and has not been done there.**

## Diagnosing a starving DVI queue

Three distinct causes needing opposite fixes (#84–#94). Identify the *shape*
before optimising anything:

1. **Burst** — a whole-frame render or CPU run before the first scanline is
   submitted. Mean work looks fine. Fix: make work per-scanline. Deeper
   queue buys time here.
2. **Throughput** — mean work exceeds the frame budget. A deeper queue does
   *nothing*; there is no surplus to refill from. Fix: cheaper work.
3. **A burst outside the loop you are measuring** — the killer, because
   every total and every per-scanline instrument says the frame is fine.
   Found only by measuring the *resource*, not the cost.

The instrument that finds #3: **lowest queue level, bucketed by position in
the frame, compared against a configuration that works.** Donkey Kong's
landscape read `band_minq 0/0/7/17/...` where tate read `23/27/31/...` — and
tate did *more* work per band. Less work with a worse queue is a
contradiction that points straight at what you are not measuring. (It was
audio running twice.)

## Verifying a change that is supposed to alter the picture

Byte-identical frame comparison is the default regression check, but it is
the wrong instrument for an interleave change (#20, #34, #36): each scanline
now reflects mid-frame VRAM by design, so the picture legitimately differs.
Compare the **emulation** instead — `invaders_host --digest-every N` hashes
registers, SP, PC, flags, all 64K, and the shift register, and the A/B is
two binaries from the same tree with one directory swapped:

```sh
MACHINE_SRC=/tmp/old/src/machines/invaders OUT=/tmp/invaders_host_old \
  ./tools/invaders_host/build.sh
```

**Always run a negative control.** The first one tried here (shifting a
`shoot` press by one frame) changed nothing — the machine was in attract
mode with nothing to shoot. Shifting the *coin* press diverges immediately.
A control that does not diverge invalidates the matching result it was
supposed to validate.

## Checklist

- [ ] ROM set CRC32s match MAME's published values
- [ ] Every hardware fact cited to driver, file and function, in the header
- [ ] Six modules present and named `<game>_*`
- [ ] Machine includes nothing from `src/boards/`
- [ ] `tools/<game>_host/` builds and runs
- [ ] Rotation default from the `ROT` flag, then verified by render
- [ ] Example sketch under `examples/Games/` with a `sketch.yaml` opt level
- [ ] Interleaved: no gap over ~2ms between scanline submissions
- [ ] `work` measured with blocked time subtracted, on device
- [ ] Asset-load failure names the missing files, reported from `loop()`
- [ ] Verified on hardware in all four rotations, **in gameplay, not attract**
- [ ] `reuse lint` passes
- [ ] A `DEVNOTES.md` entry for anything that surprised you
