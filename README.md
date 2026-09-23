<!--
SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
SPDX-License-Identifier: MIT
-->

# Adafruit Arcade Machines

Classic arcade games for the [Adafruit Fruit Jam](https://www.adafruit.com/product/6200)
(RP2350B) and the [Feather ESP32 V2](https://www.adafruit.com/product/5438), running under the Arduino framework instead of the raw Pico SDK.

This started as an Arduino port of [adafruit/invaders_pico](https://github.com/adafruit/invaders_pico)
(itself a Pico SDK port of Space Invaders), restructured as **SAMP** —
Single Arcade Machine Port — a small framework for building one-game,
one-board arcade firmware, organized so the pieces that *aren't* specific
to one game or one board are reusable for the next port:

- **`src/cpu/`** — CPU interpreters, with no hardware or game knowledge at
  all: `i8080`, plus `z80`, `mcs48` and `m6502`, added for the
  Namco/Nintendo games, for Donkey Kong's sound board, and for Burger Time.
- **`src/hal/`** — plain C function contracts (video/audio/input/storage).
  No implementation lives here.
- **`src/machines/`** — one directory per game (`invaders/`, `lrescue/`,
  ...): that game's own port wiring, VRAM renderer, sound, and ROM/asset
  manifest. Board-agnostic — talks only to the HAL.
- **`src/boards/fruitjam/`** — the Fruit Jam's implementation of those
  contracts: PicoDVI video, TLV320DAC3100 + I2S audio, GPIO input, SD card
  storage on SdFat.
- **`src/boards/feather_esp32/`** — a second board, proving the split is
  real: Feather ESP32 V2 + 2.4" TFT FeatherWing (SPI ILI9341) + a
  MAX98357A. Entirely different silicon, display technology and audio
  path; not one line of `src/cpu/` or `src/machines/` changed to add it.
- **`src/arch/`** — the layer between the two: chip facts rather than board
  facts (RP2 PIO/DMA, ESP32 I2S and SPI), shared by every board built on
  that family.
- **`examples/Games/`** — one sketch per game, each the one place that knows
  both "this game" and "this board," wiring the two together.

This is a single Arduino library: install it, then open an example. The
three axes above are directories inside `src/`, not separate libraries, so
every example compiles the whole tree and the linker discards what that
game does not use — verified byte-for-byte, it costs nothing.

`src/cpu/z80/` and `src/machines/pacman/` (below) added the project's first
Z80-based game this way — a sibling library alongside the i8080 axis, not a
replacement for it. `src/boards/feather_esp32/` later did the same for a
second board — see `../CLAUDE.md` in the parent
`i8080/` checkout (if you have it) for the full architecture rationale, or
just read `src/hal/*.h` for the contracts themselves.

## Games

| Game | Sketch | Notes |
|---|---|---|
| Space Invaders | [`invaders_fruitjam/`](examples/Games/invaders_fruitjam/README.md) | The original port; sample-based sound only. |
| Lunar Rescue | [`lrescue_fruitjam/`](examples/Games/lrescue_fruitjam/README.md) | Same "8080bw" board family, plus one genuinely *synthesized* (bit-banged) audio channel — see its README and `extras/DEVNOTES.md` problems #12-17 for what that took. |
| Pac-Man | [`pacman_fruitjam/`](examples/Games/pacman_fruitjam/README.md) | The project's first **Z80**-based port (`src/cpu/z80/`) and first tile+sprite video hardware (`src/machines/pacman/`), with fully synthesized Namco WSG sound — built from scratch against the real ROM/PROM dump and verified against MAME's driver source; see its README for citations. |
| Galaga | [`galaga_fruitjam/`](examples/Games/galaga_fruitjam/README.md) | The project's first **multi-CPU** machine — three Z80s sharing RAM — plus a Namco 06XX/51XX/54XX custom I/O chain and a fourth video layer (the 05XX starfield). Synthesized WSG *and* 54XX explosion audio. |
| Ms. Pac-Man | [`mspacman_fruitjam/`](examples/Games/mspacman_fruitjam/README.md) | The project's first machine that is another machine **plus a daughterboard**: stock Pac-Man hardware with the aux board's three extra ROMs, an address/data-line **encrypted** program bank, and eight address ranges that flip banks on any access. First **banked** address space and first ROM decode in the project. |
| Donkey Kong | [`dkong_fruitjam/`](examples/Games/dkong_fruitjam/README.md) | The project's first **Nintendo** board and first **DMA-driven** sprites — the Z80 never writes sprite RAM, an i8257 controller does. Also its first **active-high** inputs, first **NMI** interrupt, and first **resistor-network** palette. Sound is an emulated **8035 sound CPU** (`src/cpu/mcs48/`, the project's third CPU axis) driving a DAC, plus approximations of its discrete analog channels tuned against recordings of a real machine. |
| Burger Time | [`btime_fruitjam/`](examples/Games/btime_fruitjam/README.md) | The project's first **6502** machine (`src/cpu/m6502/`), first **encrypted-opcode** CPU (a DECO CPU-7, descrambled in the fetch path rather than at load time), first game with **no vblank interrupt at all** — it polls a vblank bit wired into a DIP-switch port — and first **palette held in RAM** rather than a PROM. Two 6502s driving two emulated **AY-3-8910s** — the project's first PSG — through a band-pass filter derived from the board's measured component values. Needed a real optimisation pass to fit the frame (23.6ms → 14ms); see `extras/DEVNOTES.md` §59-64. |

Each game's own README covers its specific ROM/sample layout, controls, and
any known quirks. They all share the building steps below.

### A second board: Feather ESP32 V2

**All seven games run here too**, each at 100% of its own machine's refresh
rate, on a Feather ESP32 V2 + 2.4" TFT FeatherWing (ILI9341) + a
MAX98357A. Not one line of `src/cpu/` or `src/machines/` differs between the
two boards.

| Game | Sketch | Emulated rate | On screen |
|---|---|---|---|
| Space Invaders | [`invaders_featheresp32/`](examples/Games/invaders_featheresp32/) | 59.542Hz | 29.8fps |
| Lunar Rescue | [`lrescue_featheresp32/`](examples/Games/lrescue_featheresp32/) | 60.037Hz \* | 30.0fps |
| Pac-Man | [`pacman_featheresp32/`](examples/Games/pacman_featheresp32/) | 60.606Hz | 30.3fps |
| Galaga | [`galaga_featheresp32/`](examples/Games/galaga_featheresp32/) | 60.606Hz | 30.3fps |
| Ms. Pac-Man | [`mspacman_featheresp32/`](examples/Games/mspacman_featheresp32/) | 60.606Hz | 30.3fps |
| Donkey Kong | [`dkong_featheresp32/`](examples/Games/dkong_featheresp32/) | 60.606Hz | 30.3fps |
| Burger Time | [`btime_featheresp32/`](examples/Games/btime_featheresp32/) | 57.445Hz | 28.7fps |

\* Lunar Rescue's is the one number here that is **not** a cabinet
measurement. It shares Space Invaders' 8080bw board and 59.542Hz refresh, but
its machine code carries a 60.0368 calibration and its cycle budget derives
from that, so the two have to agree — see `extras/DEVNOTES.md` #120.

**The display rate is half the game rate on purpose.** The panel cannot reach
60Hz, so each sketch advances the emulator *two* frames per painted frame and
a wall-clock limiter locks the result to the rate above. The machine keeps the
interrupt cadence the real cabinet produced and only the picture is decimated.
**Game speed and display rate are therefore different numbers on this board** —
conflating them is what hid a 40%-speed bug for most of the port.

**The frame budget is per-game.** A shared 16,500us constant is right for the
four 60.606Hz machines and silently wrong for the other three; it shipped that
way in v2.7.0 and ran Burger Time 5.5% fast.

The panel is the ceiling here, not the emulator: 320x240 RGB565 at 40MHz is
30.7ms of unavoidable clocking, and the transport runs within 3% of that. See
`extras/DEVNOTES.md` #104-#111 before changing anything about that path, and
#122 for why a rate limiter must repay a missed deadline rather than forgive
it.

Two differences from the Fruit Jam sketches are worth knowing before you read
one: there is no `setup1()`/`loop1()` second-core display pump — the panel is
written from `submit_scanline()` — and `HAL_BTN_MIRROR` and `HAL_BTN_STRETCH`
are not wired on this board and read false.

### Screen orientation and aspect ratio

Every game runs in **all four rotations** — upright, 90° CCW ("tate", the
rotated-monitor orientation these cabinets used), 180°, and 90° CW — plus an
independent horizontal mirror for Pepper's-Ghost cabinets. All seven are
verified on physical hardware in each rotation, in gameplay rather than
attract mode.

**Every game boots into tate and expects a monitor turned into portrait.**
Which of the two tate values a given game uses differs — the Namco and
8080bw cabinets mounted their tubes opposite ways, so Pac-Man, Ms. Pac-Man
and Galaga default to rotation 1 while Space Invaders, Lunar Rescue, Donkey
Kong and Burger Time default to 3 — but **all seven come up upright together
on one screen, turned one way, with no button presses.** That direction was
chosen to match the way real portrait monitor stands rotate. If your display
turns the other way, press ROTATE twice on each game.

Three front-panel buttons control the display, and they are the same on every
game:

| Button | |
|---|---|
| **1** | Aspect-ratio correction on/off |
| **2** | Cycle rotation |
| **3** | Horizontal mirror |

**Button 1 is the one that depends on your monitor, not on the game.** These
cabinets had 4:3 tubes, so a correct picture fills the screen in the same
4:3 proportion. A 16:9 panel rotated for tate already stretches the image on
its own, and the correction would over-correct it; a wide panel forced to 4:3,
or a real 4:3 panel, needs the correction. Only the person looking at the
screen can say which, so it is a button rather than a build option. It is off
by default (on for Pac-Man and Ms. Pac-Man, whose native raster is already
close to 4:3).

**None of the three settings is remembered across a power cycle yet** —
rotation, mirror and aspect correction all reset to their defaults at boot.
Persisting them is planned future work; see `extras/DISPLAY_GEOMETRY.md` section 8
and `extras/DEVNOTES.md` #91 for the design and the one hazard it has to avoid.

`extras/DISPLAY_GEOMETRY.md` has the derivation, the per-game measurements, and the
frame-budget cost of each option.

### Prebuilt firmware

If you'd rather not install the toolchain, every [release](../../releases)
carries **all seven games for both boards** — fourteen files in total.

**Fruit Jam — `<game>_fruitjam.uf2`.** Hold **BOOT** while connecting USB (or
hold BOOT and tap **RESET**), then copy the `.uf2` onto the `RP2350` drive
that appears.

**Feather ESP32 V2 — `<game>_featheresp32.bin`.** That board has no
drag-and-drop bootloader, so it flashes over serial instead: write the file
at offset `0x0` with Adafruit's browser tool,
[Web Serial ESPTool](https://learn.adafruit.com/circuitpython-with-esp32-quick-start/web-serial-esptool).
Each `.bin` is a complete flash image — bootloader, partition table and
application at their correct offsets in one file — not a bare application
binary.

The binaries contain no ROM data — you still need the microSD card with
legally-obtained ROMs described in each game's README, for either board. Each
release build uses the optimization level that game's own `sketch.yaml` pins,
which is not the same for every game; see below.

To build the whole set yourself, ready to attach to a release:

```bash
./extras/dist/build_all.sh      # seven <game>_fruitjam.uf2 and seven <game>_featheresp32.bin
```

`extras/dist/` is gitignored apart from that script and its README — the binaries
belong on a release, not in the tree.

## Building

### Arduino IDE

The two boards need different cores, different libraries and a different
way of setting the optimisation level, so they are split here.

#### Fruit Jam

1. Install the **Raspberry Pi Pico/RP2040/RP2350** board core (Earle
   Philhower's, via Boards Manager — add
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   as an additional board URL first).
2. Install this library. Either **Sketch → Include Library → Add .ZIP
   Library** on a download of this repo, or clone it into your sketchbook's
   `libraries/` folder. Its one dependency, `PicoDVI - Adafruit Fork`, comes
   from Library Manager (it is listed in `library.properties`, so the IDE
   offers to install it for you).

   > **Upgrading from an earlier checkout? Undo the old sketchbook setting.**
   > This repo used to be an Arduino *sketchbook*, and the old instructions
   > said to point **Preferences → Sketchbook location** at its root. That is
   > now wrong and produces
   > `fatal error: Adafruit_Arcade_Machines.h: No such file or directory` — the IDE
   > looks for libraries in `<sketchbook>/libraries/`, and this repo *is* the
   > library, so it cannot be inside its own `libraries/` folder. Set
   > Sketchbook location back to your normal one (`~/Documents/Arduino` on
   > macOS) and install the library as above.
3. Select board **Adafruit Fruit Jam RP2350**.
4. Set **Tools → Optimize** to `-O2` or `-O3` — each example's `sketch.yaml`
   says which, and the header comment at the top of every `.ino` repeats it.
   The core's default `-Os` is not fast enough for any game here.
   **If you get this wrong the build now stops with an explanatory error**
   rather than producing firmware that looks broken: at `-Os` Ms. Pac-Man
   needs 19.5ms of a 16.66ms frame and goes solid red (`extras/DEVNOTES.md` #49),
   Galaga needs 17.8ms and flashes red throughout play (#35) — and red on
   this board is *also* the missing-SD-card colour, which made it an
   expensive mistake to diagnose. `arduino-cli` reads `sketch.yaml`
   automatically; the IDE does not always.
5. Prepare an SD card (FAT32, **MBR** partition scheme — not GPT/exFAT,
   which macOS Disk Utility defaults to on "Erase") with that game's own
   ROM/sample layout — see its README.

   On macOS, Disk Utility's GUI gives you GPT, so use the command line:

   ```bash
   diskutil list external physical                       # find the card
   diskutil eraseDisk FAT32 ARCADE MBRFormat /dev/diskN  # N from above
   ```

   `MBRFormat` is the whole point. **Read the identifier off that first
   command every time** — disk numbers are handed out in attach order and
   move between reboots, so today's `disk5` is tomorrow's something else, and
   the command erases whatever you name without asking. Target the whole
   disk (`/dev/disk5`), never a slice (`/dev/disk5s1`), and keep the volume
   name to 11 characters or fewer.

   Check it with `diskutil list /dev/diskN`: line 0 must read
   `FDisk_partition_scheme`, which is MBR. The filesystem line may come back
   `DOS_FAT_32` or `DOS_FAT_16` — both are fine, and small cards often get
   FAT16 whatever you ask for. SdFat is built here with `SDFAT_FILE_TYPE 1`
   and `FAT12_SUPPORT 1`, so it reads FAT12/16/32 and only excludes exFAT.
   What it cannot read is GPT: `FatPartition::init()` takes the start sector
   straight out of the MBR partition table and never looks for a GPT header,
   so on a GPT disk it lands on the protective entry and finds no BPB. That
   is why the partition scheme is the part that matters.
6. **File → Examples → Adafruit Arcade Machines → Games →** your game, and
   upload.

Before a full game, it's worth flashing the standalone tests under
**Examples → Adafruit Arcade Machines → SelfTest** to confirm each subsystem
independently: `input_test_fruitjam` → `dvi_test_fruitjam` →
`audio_test_fruitjam` → `sd_test_fruitjam`.

There is also `input_bounce_test_fruitjam`, which is not a smoke test but a
contact profiler: it samples every button at 10kHz and reports what a
once-per-frame sampler would have seen. Reach for it when an input misbehaves
in a way `input_test_fruitjam` (which polls at 50Hz behind a `delay(20)`, and
so is deliberately blind to bounce) cannot show. Read `extras/DEVNOTES.md` #32 before
trusting what it tells you.

#### Feather ESP32 V2

1. Install the **esp32** board core (Espressif's, via Boards Manager — add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` as an
   additional board URL first).
2. Install this library exactly as above. Its dependencies **differ from the
   Fruit Jam's**: this board needs `SdFat - Adafruit Fork` and
   `Adafruit ILI9341` (which pulls in `Adafruit GFX` and `Adafruit BusIO`),
   and does **not** need `PicoDVI - Adafruit Fork`. I2S audio comes from the
   core's own `ESP_I2S`, so there is nothing to install for sound.
3. Select board **Adafruit Feather ESP32 V2**.
4. **There is no Tools → Optimize on this core** — do not go looking for it.
   The level is pinned per sketch by a `build_opt.h` file sitting beside the
   `.ino` (it contains one line, `-O3`), which is the IDE's own documented
   per-sketch flag mechanism. `sketch.yaml` cannot do it here: it carries the
   FQBN but not build properties, and adding one there silently leaves you on
   `-Os`. **Keep `build_opt.h` with the sketch if you copy it anywhere** —
   without it you get the core default and the same red screen the Fruit Jam
   gives for the same reason.
5. Assemble the hardware: a **2.4" TFT FeatherWing** (#3315) on top of the
   Feather, and for sound an **I2S 3W Class-D amp, MAX98357A**
   ([#3006](https://www.adafruit.com/products/3006)) wired BCLK→`27`,
   LRC→`12`, DIN→`13`, GND→GND and **Vin→3V**. The stereo pair (#6513)
   works unchanged if you have one — the games mix to mono, so there is no
   second channel to lose. Nine buttons go active-low to ground; four of
   them — A2, A3, A4 and GPIO 37 — land on input-only pads with no internal
   pull-up and need an **external resistor to 3V3**: 10K is the sensible
   pick, 4.7K–47K is comfortable, and 1K–100K works. Below 1K is just wasted
   current while a button is held; above 100K the noise margin starts to
   matter on long cabinet wiring. The other five buttons already run on the
   ESP32's internal pull-up at a nominal ~45K, so the middle of that range is
   the measured status quo rather than a guess. The full pinout, and why
   40MHz is a hard ceiling on this wing, is in
   `src/boards/feather_esp32/board_config_feather_esp32.h`.

   **The amp's Vin belongs on 3V**, which is the only one of the three rails
   that is right however the board is powered:

   | | USB, no battery | Battery only | USB + battery |
   |---|---|---|---|
   | **VBUS** | loudest | **dead** | loudest |
   | **3V** | fine | fine | fine |
   | **BAT** | **crunchy** | great | great |

   **VBUS** is the loudest — 5V is what the 3W rating assumes — and is dead
   on battery, so a board wired that way plays on the bench and goes silent
   the moment it is unplugged, with picture and input still working.
   **BAT** is better than 3V *once a cell is actually fitted*: 3.7–4.2V, so
   most of VBUS's volume, and upstream of the regulator the ESP32 and panel
   share. With no cell it is not a battery rail at all but the charger's
   output, which has no reservoir — the amp's current bursts sag it at audio
   rate and it sounds crunchy (a 470µF–1000µF cap at Vin substitutes for the
   missing cell). **3V** is the quietest, at roughly 44% of VBUS's power, and
   in principle shares its regulator with the ESP32 and the panel — not
   something seen here. It wins on the middle row: there is no way to power
   this board that it gets wrong.

   **Either revision of the wing works.** #3315 shipped as V1 until Adafruit
   redesigned it on 2023-10-11 and as V2 since, so that part number buys a V2
   today; this port was brought up on a V1. Same ILI9341, same microSD, same
   pins — the only change that reaches the code is the touch controller
   (V1 STMPE610 on SPI, V2 TSC2007 on I2C), which this port does not use but
   does have to leave alone correctly. See `FEATHER_TOUCH_CS_IRQ` in the board
   config for why that pin is `INPUT_PULLUP` on both and must not be driven.
6. Prepare the SD card exactly as for the Fruit Jam (FAT32, **MBR** — the
   `diskutil eraseDisk ... MBRFormat` recipe in step 5 above) and put it in
   the FeatherWing's microSD slot.
7. **File → Examples → Adafruit Arcade Machines → Games →**
   `<game>_featheresp32`, and upload over USB serial.

The `SelfTest` examples are Fruit Jam only; there are no ESP32 equivalents
yet. Each game prints a once-per-30-frames heartbeat over serial at 115200
instead, reporting display fps, emulated fps, percentage of the machine's
real rate, rotation and audio ring margin — which is the fastest way to tell
a wiring problem from a performance one.

### arduino-cli

**Fruit Jam:**

```bash
arduino-cli core install rp2040:rp2040 \
  --additional-urls https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
arduino-cli lib install "PicoDVI - Adafruit Fork"

# --library points the builder at this checkout, since the repo IS the
# library rather than something installed under your sketchbook.
arduino-cli compile --library . examples/Games/invaders_fruitjam
```

Each Fruit Jam example's `sketch.yaml` pins its own required `opt=` level as
the default `--fqbn`, so it can be omitted.

**Feather ESP32 V2:**

```bash
arduino-cli core install esp32:esp32 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "SdFat - Adafruit Fork" "Adafruit ILI9341"

arduino-cli compile --library . --fqbn esp32:esp32:adafruit_feather_esp32_v2 \
  examples/Games/invaders_featheresp32
```

Here `sketch.yaml` carries only the FQBN — the optimisation level comes from
the `build_opt.h` beside each `.ino`, which `arduino-cli` picks up
automatically from the sketch directory. Upload over USB serial with
`arduino-cli upload -p <port> --fqbn esp32:esp32:adafruit_feather_esp32_v2 ...`;
`arduino-cli board list` will name the port.

`./extras/dist/build_all.sh` builds all fourteen images — seven of each — the
same way.

### Working in a checkout (rather than on an installed copy)

If you are editing this library, do not point the IDE's sketchbook at the
repo — see the note above. Symlink the checkout into your real sketchbook
instead, so edits are live in the IDE with no copying:

```bash
ln -s "$PWD" ~/Documents/Arduino/libraries/Adafruit_Arcade_Machines
# into that same sketchbook -- both boards' dependencies
arduino-cli lib install "PicoDVI - Adafruit Fork" \
  "SdFat - Adafruit Fork" "Adafruit ILI9341"
```

Then **File → Examples → Adafruit Arcade Machines** lists every example, and
opening an
example's `.ino` directly from the checkout works too, because the include
resolves through the installed link.

Opening an example `.ino` from a checkout that is *not* installed anywhere
does **not** work: the builder does not infer the enclosing library from an
`examples/` path. That is why `arduino-cli` needs `--library .` above.

### Continuous integration — not set up yet

There is no `.github/workflows/` here; everything is run by hand. What
follows is what it would take, because it is not quite drop-in.

The right tool is Adafruit's standard
[ci-arduino](https://github.com/adafruit/ci-arduino): copy its
`example_actions.yml` to `.github/workflows/githubci.yml` and set
`PRETTYNAME`. It runs clang-format, Doxygen, and an example build per
platform, and it installs whatever `library.properties` lists in `depends`
automatically — which only works at all now that there *is* a
`library.properties`.

**The restructure is what makes it reachable.** `build_platform.py` starts at
`<repo>/examples` and recurses into subdirectories, so `examples/Games/` and
`examples/SelfTest/` are both found. Before, the sketches sat at the top
level with no `examples/` directory, so it would have discovered zero of
them — and the `.fruit_jam.test.only` markers beside each sketch, which
ci-arduino looks for in each example's own directory, were inert.

Four things have to be settled before turning it on:

1. **`fruit_jam` is a valid platform id, but it is not in the default
   `main_platforms` group** (that is uno, leonardo, mega2560, zero, qtpy_m0,
   esp8266, esp32, metro_m4, trinket_m0). The workflow has to name
   `fruit_jam` explicitly. Left on the default, every example with a
   `.fruit_jam.test.only` marker is skipped for every platform in the group,
   and CI passes green having built nothing.
2. **The blocker: ci-arduino's stock `fruit_jam` FQBN carries no `opt=`
   field**, so it builds at the core default `-Os` — which trips the
   optimisation guard in `src/Adafruit_Arcade_Machines.h` and fails all 13
   examples.
   That is the guard doing its job, not a false alarm: an `-Os` build of any
   game here red-screens on hardware. Resolving it means either getting
   `opt=Optimize3` into that FQBN upstream, or writing a small local workflow
   that honours each example's own `sketch.yaml`. There is no per-example
   FQBN override in ci-arduino.
3. **clang-format and Doxygen would both need switching off** (`#` them out
   in the YAML). There is no `.clang-format` and no Doxyfile in this repo,
   and the source is not clang-formatted.
4. `.fruit_jam.generate` produces UF2 binaries in CI, which could replace
   `extras/dist/build_all.sh` for release artefacts — but only once point 2 is
   fixed, or the UF2s it produced would be the red-screening `-Os` builds.

Two checks worth adding alongside it, both seconds long and neither part of
ci-arduino: `reuse lint`, and `arduino-lint --library-manager submit` run
against a clean export (see below for why the export matters). Building the
host harnesses under `extras/tools/` is worth adding too — they are what
enforces the architecture rule now that separate libraries no longer do: a
machine that reached into `src/boards/` would compile fine as part of the
library and fail against the host stub.

Three failures this project has already had are the kind CI catches, all of
them invisible until something is built from a clean checkout: thirteen
broken symlinks that reached `main` (a local tree had real directories in
their place, so nobody saw it); a symbol collision between two machines that
only appears once both are in one build, which is now every build; and bare
`#include "z80.h"` instead of `#include "cpu/z80/z80.h"`, since `src/`
subdirectories are not on the include path.

**Be clear about what it would not cover.** No ROMs live in this repo and no
runner has a Fruit Jam, so CI can compile the examples and build the host
harnesses but never run a game: no frame timing, no starvation counts, no
audio, no digest A/Bs. A green check would mean "still compiles," never
"still works." The hardware loop in *Debugging on hardware* below stays the
only real verification — every game in the table above was checked by
flashing it.

### `arduino-lint`, and the two ways to run it wrong

`arduino-lint` is what Library Manager submissions are checked against. This
library **passes**: 0 errors, 1 warning, exit 0, with all 20 examples clean —
fourteen games (seven per board) and six SelfTest sketches.

```bash
# lint what the registry would actually clone, NOT the working tree
git archive HEAD | tar -x -C /tmp/aam-clean
arduino-lint --library-manager submit /tmp/aam-clean
```

Two things will give you a false failure if you skip that first line, and
both cost time here before being understood:

1. **Run it on a clean export, not the working directory.** `arduino-lint`
   walks the filesystem rather than git, so the gitignored `libraries/`
   folder — where `arduino-cli lib install` puts third-party dependencies
   when this repo is your sketchbook — gets linted as if it were ours. That
   produces a wall of *"Sketch(es) found outside examples and extras
   folders"* naming `Adafruit_NeoPixel/examples/…` and friends. None of it is
   in the repo.
2. **Do not pass `--compliance strict`.** The registry does not, and strict
   promotes rule LP015 to an error: *"library.properties name Adafruit Arcade
   Machines contains spaces."* At the default level it is a warning, and its
   own text says spaces are supported. Every Adafruit library in the index
   has them ("Adafruit GFX Library", "Adafruit NeoPixel"), so the name
   follows the catalogue rather than the lint hint. That single warning is
   the only finding.

The layout is what unblocked this: everything the Arduino spec does not
recognise at the top level (development notes, host harnesses, release
staging) now lives under `extras/`, which the spec reserves for exactly that
and the IDE ignores entirely. Root is `src/`, `examples/`, `extras/`,
`library.properties`, `README.md`, `PORTING.md` and the licence files.

Submission itself is a separate decision: a PR to
[`arduino/library-registry`](https://github.com/arduino/library-registry)
adding this repo's URL to `repositories.txt`. The release tags already line
up (`v2.10.2` matches `version=2.10.2` in `library.properties`), and all four
dependencies — `PicoDVI - Adafruit Fork`, `SdFat - Adafruit Fork`,
`Adafruit TLV320 I2S` and `Adafruit ILI9341` — are themselves in Library
Manager. Two of the four are per-board rather than universal: PicoDVI is
Fruit Jam only and ILI9341 is Feather ESP32 V2 only, but `depends` has no way
to say so, so both are declared.

## Host test harnesses

`extras/tools/` builds any `src/machines/<game>/` into a **native executable**
that runs the real machine — actual CPU cores, real ROMs, real port decode,
real per-scanline interleaving — on your development machine, against a stub
the HAL. This works only because SAMP's architecture rule holds: a machine
talks exclusively through the HAL's 21 functions, so the host stub
is just a fourth "board".

```sh
./extras/tools/galaga_host/build.sh   && ./extras/tools/galaga_host/galaga_host     --frames 5000
./extras/tools/pacman_host/build.sh   && ./extras/tools/pacman_host/pacman_host     --frames 5000
./extras/tools/invaders_host/build.sh && ./extras/tools/invaders_host/invaders_host --frames 5000
./extras/tools/mspacman_host/build.sh && ./extras/tools/mspacman_host/mspacman_host --frames 5000
./extras/tools/dkong_host/build.sh    && ./extras/tools/dkong_host/dkong_host       --frames 5000
./extras/tools/btime_host/build.sh    && ./extras/tools/btime_host/btime_host       --frames 5000 \
                                    --rom ../btime_assets/rom
```

A hardware iteration costs minutes; this costs about a second, with unlimited
tracing, real frame rendering to PPM, and audio captured to WAV. Most of the
hard bugs in this project turned out to live in the emulated machine, where
this is by far the fastest place to find them. See `extras/tools/README.md`.

## Debugging on hardware

Every game here is flashed firmware with no OS, so the loop is the same on
both boards: add an instrument, reflash, read the serial line. Reflashing
takes seconds, which is faster than attaching a debugger and it leaves the
instrument behind for next time. What differs is how you flash and what the
heartbeat tells you.

For anything about the emulated *machine* rather than the board, use the host
harnesses above instead — they answer the same questions in about a second.

### Fruit Jam

`arduino-cli upload` is a 1200-baud touch into BOOTSEL followed by a UF2
copy. **No SWD/OpenOCD/Debug Probe is needed or used** — earlier sessions
fought 75–200 second SWD loads before working this out. See
`extras/DEVNOTES.md`'s "How hardware debugging actually works on this
project".

Each sketch prints a once-per-second heartbeat:

```
[dkong] frame 1980, frame 16665us (work 11815us, blocked 4850us), work_max 15563us, audio 3045us
```

`frame` on its own tells you nothing — `hal_video_acquire_scanline()`
blocks, so it pins at the DVI frame period as soon as the work fits.
**`work` is the real cost** and `blocked` is the slack (`extras/DEVNOTES.md` #25).
Reading it while a serial monitor is open needs the port free — see
`extras/DEVNOTES.md` for the Arduino IDE Serial Monitor conflict.

### Feather ESP32 V2

This board flashes over USB serial (`arduino-cli upload -p <port>`), and the
same port carries the log at 115200. There is no bootloader button dance.

The heartbeat is a different shape, once every 30 frames, because the
questions are different — this board's display rate is fixed by SPI
bandwidth, so the number that matters is whether the *game* is keeping its
own rate:

```
[btime-esp32] frame 180  28.7 fps display  57.5 fps emulated (100% of 57.4Hz)  frame 34427 us  rot 3  audio ur 0 ov 0 queued 666 min 572 (drain 256)
```

- **`fps display` and `fps emulated` are different numbers on purpose**, the
  second being twice the first; conflating them hid a 40%-speed bug for most
  of the port. The percentage is the one to read — it should be 100%.
- **`rot`** makes a stray ROTATE press visible. GPIO 37 is input-only with no
  internal pull, so an unwired or floating button line cycles it silently.
- **`min` is the audio ring depth the I2S task saw at the *start* of a call,
  and it must stay above the 256 it drains per call.** `ur` must be 0. The
  producer's own peak and overrun counters can read perfectly healthy while
  the ring runs dry mid-block — this is the number that actually predicts a
  click (`extras/DEVNOTES.md` #119).

A `Guru Meditation Error` backtrace on this board decodes to file and line
with the core's own `addr2line` against the `.elf` in the sketch's build
directory:

```bash
xtensa-esp-elf-addr2line -pfiaC -e <sketch>.ino.elf <backtrace addresses>
```

## Porting a new machine

`PORTING.md` is the distilled method — the order of work that has produced
seven ports (one of them with zero hardware debug cycles), the traps ranked
by how much time they actually cost, and the performance levers in the order
they paid off. Read it before starting; it is short, and every rule in it is
a bug that already shipped once.

A new game is one directory under `src/machines/` and one sketch under
`examples/Games/`. Nothing else in the tree has to change.

## More detail

See `extras/DEVNOTES.md` for the full account of every real bug found while
bringing this up on actual hardware, across both the shared framework and
each individual game port — several of the fixes there
(`dvi_vertical_repeat`, the scanline-buffer ceiling that turned out not to
be one, the `-Os`-isn't-fast-enough finding, the cycle-vs-real-time
audio-clock lesson)
are non-obvious and worth reading before touching `src/boards/fruitjam/`,
`src/cpu/i8080/`, or adding a new synthesized-audio channel to any game.

`extras/HDMI_AUDIO_NOTES.md` is a possible future feature rather than a
record of the present: audio over the HDMI cable on the Fruit Jam, which
would mean replacing the PIO-bitbanged DVI backend with an HSTX one. Nothing
is implemented; the file exists so the research does not have to be repeated.

## Credits

- Original Space Invaders emulator: [shotto42/invaders](https://github.com/shotto42/invaders)
- 8080 CPU core: [intarga/i8080e](https://github.com/intarga/i8080e) (MIT)
- Z80 CPU core: [superzazu/z80](https://github.com/superzazu/z80) (MIT)
- MCS-48 CPU core (`src/cpu/mcs48/`): **ported from**
  [MAME](https://github.com/mamedev/mame)'s `mcs48_cpu_device`
  (`src/devices/cpu/mcs48/mcs48.cpp`) — **BSD-3-Clause**, copyright Dan
  Boris, Mirko Buffoni, Aaron Giles, Couriersud. This is the one library
  here that follows MAME's *code* rather than only its documented hardware
  facts, so it carries MAME's licence instead of this project's MIT; see
  `src/cpu/mcs48/mcs48.c`'s header for exactly what came from MAME
  and what didn't.
- Pac-Man's memory map, I/O map, tile/sprite/palette decode, and Namco WSG
  sound register map were all verified against
  [MAME](https://github.com/mamedev/mame)'s `pacman` driver source, not
  ported from any existing emulator — see `src/machines/pacman/`'s own
  file-header comments for exact citations.
- Ms. Pac-Man's aux-board ROM decode (the address/data-line bitswaps and the
  40 eight-byte patches) and its banked address map were transcribed from
  the same driver's `init_mspacman()`, `mspacman_install_patches()` and
  `mspacman_map()` — see `src/machines/mspacman/`'s file headers.
- Galaga's memory map, 3-CPU interrupt/NMI scheme, tile/sprite/palette
  decode and discrete audio component values were verified against
  [MAME](https://github.com/mamedev/mame)'s `galaga` driver, its
  `namco06`/`namco51`/`namco54` device sources and its `galaga_a.cpp`
  netlist. The starfield follows `starfield_05xx.cpp`, itself a pin-level
  reverse-engineering of a real 1981 Namco 05XX by R. Hildinger (2019).
  Because modern MAME emulates the 51XX/54XX at low level (running their
  real MB8843/MB8844 firmware, which this project has no dump of), those two
  chips are hand-written HLE, with
  [danjulio/gcore_galagino](https://github.com/danjulio/gcore_galagino) as
  the behavioural reference — see `src/machines/galaga/`'s file-header
  comments for what is cited and what is approximated.
- ESP32 architecture: [harbaum/galagino](https://github.com/harbaum/galagino)
  by Till Harbaum — a Galaga/Pac-Man/Donkey Kong emulator for the ESP32, and
  the upstream of `gcore_galagino` above. **No code was taken from it**; it was
  the reference this project reasoned *against* while bringing up the Feather
  ESP32 V2, and it is cited in the files where the same problem came up:
  - running emulation and video on **separate cores** behind a notify
    handshake, rather than interleaving them — its own note is "let the cpu
    emulation run on the second core, so the main core can completely focus
    on video" (`src/machines/galaga/galaga_machine.cpp`,
    `examples/Games/galaga_featheresp32/`);
  - advancing the emulator **two frames per painted frame**, so a panel that
    cannot reach 60Hz decimates the *picture* rather than the game
    (`examples/Games/btime_featheresp32/`, `galaga_featheresp32/`);
  - the consequence of that trade, which is the part easy to miss: anything
    the **renderer** animates rather than the emulated machine needs its step
    scaled to match. Galaga's starfield is exactly that
    (`src/machines/pacman/pacman_machine.cpp`,
    `src/machines/galaga/galaga_video.cpp`);
  - letting the IDF `spi_master` driver own the transfers instead of driving
    the DMA engine by hand. galagino reaching ~30Hz on this same panel at this
    same 40MHz is what established that the silicon was fine and the
    hand-written register sequence was not — nine failed fix attempts were
    spent before that comparison was made
    (`src/arch/esp32/arch_spi_dma.h`, `extras/DEVNOTES.md` #108).

  Its full-frame `prepare_frame()` snapshot is the one design here that was
  deliberately *not* followed — this renderer latches sprites once per frame
  and reads little else live, so it does not need the stronger guarantee; see
  `galaga_machine.cpp` for that reasoning.
- 6502 CPU core (`src/cpu/m6502/`): [superzazu/6502](https://github.com/superzazu/6502) (MIT),
  the same author as the Z80 core above. Four documented changes from
  upstream (an `extern "C"` guard, a `uint32_t` cycle counter, an optional
  `read_opcode` hook for Burger Time's encrypted DECO CPU-7, and an
  illegal-opcode counter); `extras/tools/m6502_test/` runs the standard 6502 test
  suites against the vendored copy and reports cycle counts against
  upstream's published figures.
- Burger Time's memory maps, DECO CPU-7 opcode scramble, char/sprite/
  background layouts, palette format, DIP defaults and sound-CPU interrupt
  wiring were all verified against [MAME](https://github.com/mamedev/mame)'s
  `btime` driver (`src/mame/dataeast/btime.cpp`, `decocpu7.cpp`) plus
  `ay8910.cpp`, `gen_latch.cpp` and `src/emu/video/generic.cpp` — see
  `extras/BTIME_PORT_PLAN.md` and `src/machines/btime/`'s file headers.
- The Pico SDK original this project was ported to Arduino from:
  [adafruit/invaders_pico](https://github.com/adafruit/invaders_pico) — Space
  Invaders on the same Fruit Jam hardware, built against the raw Pico SDK
- DVI output: [PicoDVI](https://github.com/Wren6991/PicoDVI) by Luke Wren, via [Adafruit's fork](https://github.com/adafruit/PicoDVI)
- I2S PIO program: Raspberry Pi's [pico-extras](https://github.com/raspberrypi/pico-extras)
  (`pico_audio_i2s/audio_i2s.pio`, BSD-3-Clause), reached via pico-infoNES —
  see `src/boards/fruitjam/audio_i2s.pio`'s own header for the
  instruction-by-instruction comparison. The state-machine setup in that
  file's `% c-sdk` block is this project's own, written for the
  TLV320DAC3100.
- SD card SPI driver: [wili8jam](https://github.com/wili8jam)
- FatFs: [ChaN](http://elm-chan.org/fsw/ff/)
- Lunar Rescue's ROM/color-PROM map and sound-trigger wiring were verified
  against [MAME](https://github.com/mamedev/mame)'s `midw8080` driver
  source, not inferred by analogy — see `src/machines/lrescue/`'s
  own file-header comments for the exact formulas and where each came from.
- Donkey Kong's memory map, i8257 DMA wiring, tile/sprite layouts,
  per-scanline sprite selection and resistor-network palette were verified
  against the same project's `dkong` driver (`dkong.cpp`, `dkong_v.cpp`)
  plus `i8257.cpp` and `resnet.cpp` — see `src/machines/dkong/`'s own
  file-header comments.
