// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Pac-Man machine lifecycle -- orchestrates ArcadeCPU_Z80 + this machine's
// own port/video/audio/asset modules, talking to hardware only through
// ArcadeHAL. Same shape as invaders_machine.cpp/lrescue_machine.cpp.
#include <string.h>
#include "pacman_machine.h"
#include "pacman_ports.h"
#include "pacman_video.h"
#include "pacman_audio.h"
#include "pacman_assets.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_video_geom.h"
#include "hal/arcade_hal_audio.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"

// Z80 clock 18.432MHz/6 = 3.072MHz, frame rate (18.432MHz/3)/(384*264) =
// 60.606060... Hz -- both verified against MAME's pacman() machine_config
// (see pacman_machine.h's header comment). Unlike ArcadeMachine_Invaders'
// CYCLES_PER_FRAME (a repeating decimal needing float math and a
// carry-forward `cyc` variable across frames), these two constants divide
// out to an exact integer: 3072000 * 384 * 264 / (18432000/3) = 50688
// cycles/frame, no remainder, no carry-forward needed.
#define PACMAN_CYCLES_PER_FRAME 50688UL

void pacman_init(pacman_system *system) {
    memset(system, 0, sizeof(*system));

    z80_init(&system->cpu);
    pacman_ports_wire(system);

    // Build the canvas mapping for this raster. Must happen before the
    // first frame -- pacman_video.cpp reads av_tate/av_yoko on every
    // scanline and they are all zeroes until this runs. LONG axis first
    // (GAME_WIDTH, 288), then SHORT (GAME_HEIGHT, 224); see
    // arcade_video_geom.h for why those names and not width/height.
    av_geom_init(PACMAN_GAME_WIDTH, PACMAN_GAME_HEIGHT);

    // Aspect-ratio correction ON for this game, which is a per-game call and
    // not the module's default (see arcade_video_geom.h).
    //
    // WITHOUT it the picture is laid out in raw raster-pixel counts, so in
    // yoko it occupies 224 canvas columns against 240 rows -- 0.933 wide for
    // 1 tall, where a real cabinet's tube is 0.75. That is 24.4% too wide,
    // and on a physical display it reads exactly as "squat". Tate is only
    // 3.7% off and looks fine either way; yoko is the one that shows.
    //
    // Affordable here, measured on hardware (DEVNOTES #79): FREE in yoko,
    // because the correction NARROWS that axis (224 -> 180 columns, fewer
    // pixels emitted) -- work_max 10845us with it against 10872us without.
    // In tate it upsamples and costs +1.6ms, landing at 12029us of a 16660us
    // budget with starve 0/60. Both orientations have room.
    //
    // This is per-game on purpose: Donkey Kong cannot afford it in tate yet
    // (#78), so the module still defaults off and each machine opts in once
    // it has been measured on real hardware.
    av_geom_set_stretch(true);

    // Rotation 1, and the 8080bw family defaults to 3 -- opposite values,
    // one physical result. See "WHICH WAY UP" in arcade_video_geom.h for
    // the house convention both are calibrated against; this game is MAME
    // ROT90, which is the half that lands on 1.
    //
    // The two families genuinely differ, and that is not a bug in either
    // renderer: 1 and 3 are an exact 180 of each other and both are
    // implemented at equal cost. What differs is which end of each game's
    // NATIVE raster is the top of the player's screen, because the real
    // cabinets mounted their tubes in opposite orientations. An earlier
    // version of this line copied Invaders' default with the comment "same
    // convention as Invaders", which was precisely the wrong assumption --
    // and the two have now swapped values without that ever ceasing to be
    // true, which is the point.
    system->rotation = 1;
    system->mirror_x = false;

    hal_video_init();
}

bool pacman_load_assets(pacman_system *system, uint16_t *out_error_color) {
    pacman_rom_load_status_t rom_status = pacman_load_rom(system);
    if (rom_status == PACMAN_ROM_LOAD_NO_STORAGE) {
        *out_error_color = PACMAN_COLOR_ERROR_NO_CARD;
        return false;
    }
    if (rom_status == PACMAN_ROM_LOAD_NO_ROM_FILES) {
        *out_error_color = PACMAN_COLOR_ERROR_NO_ASSETS;
        return false;
    }

    pacman_video_build_caches();
    hal_storage_unmount();

    hal_audio_init(PACMAN_AUDIO_SAMPLE_RATE);
    pacman_audio_init(system);

    hal_input_init();
    return true;
}

// Runs the frame's cycles INTERLEAVED with scanline submission, evenly
// spreading PACMAN_CYCLES_PER_FRAME across the HAL_VIDEO_HEIGHT
// acquire/submit calls instead of running them all in one uninterrupted
// burst before the first call. Fixes arcade_arduino/DEVNOTES.md problem
// #19: even after fixing the renderer itself (problem #18), a real,
// visible stall remained because *this* loop still ran the whole frame's
// ~50,688 Z80 cycles before the frame renderer ever called
// hal_video_acquire_scanline() -- same starvation mechanism, just moved
// from the renderer into the CPU loop.
//
// THIS IS NOW THE ONLY PATH. It used to be gated to tate/CW, with
// landscape/180 falling back to a fully sequential run_frame_sequential()
// because a yoko scanline needs a native COLUMN and the renderer could only
// produce rows. pacman_video.cpp's render_native_column() removed that
// constraint, so the gate, the sequential path and the 129KB frame_cache
// are all gone together -- and with them the red those orientations showed
// every frame (DEVNOTES #18/#75).
//
// Side effect worth knowing about: because each scanline is now rendered
// from whatever VRAM/sprite state exists at that exact point in the
// frame's CPU execution (not the frame's *final* state), a scanline near
// the top of the picture can reflect slightly older game state than one
// near the bottom. This is not a new inaccuracy introduced by emulation --
// it is how real scanline-order CRT hardware actually behaves, and Pac-Man's
// sprites move only a few pixels per frame, so it should be imperceptible
// in practice; flag it if anything looks like a one-frame tear on fast-
// moving elements.
// `system->cpu.cyc` is the z80 core's own free-running uint32_t, never
// reset, so at Pac-Man's 3.072MHz it wraps roughly every 23 minutes.
// Comparing ELAPSED cycles (a subtraction) rather than an absolute target
// is what makes that safe indefinitely: unsigned subtraction wraps modulo
// 2^32 exactly as the counter does. DEVNOTES.md problem #22 is a real
// permanent hang from getting this wrong, and it is why every local here is
// uint32_t and not `long`.
// `emulated_frames` DECOUPLES GAME SPEED FROM DISPLAY RATE, and exists for
// boards whose display cannot keep up with 60Hz.
//
// The machine advances that many frames' worth of cycles and fires that
// many vblank interrupts, while painting the screen ONCE. The Z80 therefore
// sees the interrupt rate the real hardware produced -- timers, animation
// and game logic all run at authentic speed -- and only the picture is
// decimated.
//
// It is close to free, which is the point. On an SPI panel the pixel
// transfer is much slower than the work feeding it: a 320-pixel row is
// ~128us on the wire against ~60us of render plus CPU, so roughly 16ms of
// every frame is already spent waiting for the transfer. A second frame's
// worth of Z80 fits inside that wait. On the Fruit Jam, where DVI paces at
// a true 60Hz, this stays 1 and nothing changes.
//
// The cost is that a painted scanline can reflect state from anywhere in
// the emulated span rather than from one frame -- an extension of the
// intra-frame staleness this loop already has by design, just over a wider
// window. galagino makes the same trade for the same reason, and notes the
// one place it leaks: anything the RENDERER animates rather than the
// emulated hardware needs its step scaled to match. Pac-Man has no such
// element. **Galaga's starfield does** -- it is generated in
// galaga_video.cpp, not by the emulated machine, so if Galaga is ever run
// this way its scroll step must be multiplied by `emulated_frames` or the
// stars will crawl at a fraction of the right speed.
static void run_frame_interleaved(pacman_system *system,
                                  uint32_t emulated_frames) {
    if (emulated_frames < 1u) emulated_frames = 1u;
    const uint32_t total_cycles = PACMAN_CYCLES_PER_FRAME * emulated_frames;
    uint32_t start = system->cpu.cyc;
    uint32_t next_int = 1u;   // fire after the 1st, 2nd, ... frame boundary

    for (uint32_t i = 0; i < HAL_VIDEO_HEIGHT; i++) {
        // Exact proportional target delta (not repeated addition) so the
        // final slice lands exactly on the total elapsed count regardless
        // of how that divides by the scanline count.
        uint32_t target_delta =
            (uint32_t)((uint64_t)total_cycles * (i + 1) / HAL_VIDEO_HEIGHT);
        while ((uint32_t)(system->cpu.cyc - start) < target_delta) {
            z80_step(&system->cpu);
        }

        // Interior vblanks, at each emulated frame boundary this scanline
        // has passed. The last one is fired after the loop so it keeps its
        // original end-of-frame position exactly.
        while (next_int < emulated_frames &&
               target_delta >= PACMAN_CYCLES_PER_FRAME * next_int) {
            if (system->interrupt_enable) {
                z80_gen_int(&system->cpu, system->interrupt_vector);
            }
            next_int++;
        }

        uint16_t *buf = hal_video_acquire_scanline();
        pacman_video_render_scanline(system, i, buf);
        hal_video_submit_scanline(buf);
    }

    // The final vblank, fired at the end -- unchanged from when this was
    // the only one.
    if (system->interrupt_enable) {
        z80_gen_int(&system->cpu, system->interrupt_vector);
    }
}

void pacman_run_frame(pacman_system *system) {
    // Every rotation, one path. See run_frame_interleaved()'s comment for
    // why there is no longer a sequential fallback for landscape/180.
    run_frame_interleaved(system, 1u);
}

void pacman_run_frames(pacman_system *system, uint32_t emulated_frames) {
    run_frame_interleaved(system, emulated_frames);
}
