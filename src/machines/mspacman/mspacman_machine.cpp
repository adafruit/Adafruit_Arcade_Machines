// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Ms. Pac-Man machine lifecycle -- orchestrates ArcadeCPU_Z80 + this
// machine's own port/video/audio/asset modules, talking to hardware only
// through ArcadeHAL. Same shape as pacman_machine.cpp, which it is a copy
// of: Ms. Pac-Man runs on Pac-Man's board, at the same clock, with the same
// frame timing and the same single vblank interrupt. Nothing in this file
// is Ms. Pac-Man-specific except mspacman_init()'s ROM-bank reset value.
#include <string.h>
#include "mspacman_machine.h"
#include "mspacman_ports.h"
#include "mspacman_video.h"
#include "mspacman_audio.h"
#include "mspacman_assets.h"
#include "hal/arcade_hal_video.h"
#include "hal/arcade_video_geom.h"
#include "hal/arcade_hal_audio.h"
#include "hal/arcade_hal_storage.h"
#include "hal/arcade_hal_input.h"

// Z80 clock 18.432MHz/6 = 3.072MHz, frame rate (18.432MHz/3)/(384*264) =
// 60.606060... Hz -- both verified against MAME's pacman() machine_config
// (see mspacman_machine.h's header comment). Unlike ArcadeMachine_Invaders'
// CYCLES_PER_FRAME (a repeating decimal needing float math and a
// carry-forward `cyc` variable across frames), these two constants divide
// out to an exact integer: 3072000 * 384 * 264 / (18432000/3) = 50688
// cycles/frame, no remainder, no carry-forward needed.
#define MSPACMAN_CYCLES_PER_FRAME 50688UL

void mspacman_init(mspacman_system *system, mspacman_rom_bank_t *rom_storage) {
    memset(system, 0, sizeof(*system));

    // After the memset, necessarily: the struct no longer contains the ROM,
    // only a pointer to storage the sketch owns.
    system->rom = rom_storage;

    z80_init(&system->cpu);

    // Build the canvas mapping for this raster (arcade_video_geom.h). Must
    // happen before the first frame -- the renderer reads av_tate/av_yoko on
    // every scanline and they are all zeroes until this runs. LONG axis
    // first (GAME_WIDTH), then SHORT (GAME_HEIGHT).
    av_geom_init(MSPACMAN_GAME_WIDTH, MSPACMAN_GAME_HEIGHT);

    // Aspect-ratio correction on, per-game and measured (see
    // pacman_machine.cpp for the same call and DEVNOTES #79). Ms. Pac-Man
    // shares Pac-Man's raster and renderer, so it inherits the same
    // arithmetic: yoko is 24.4% too wide without it, and the correction is
    // free there because it NARROWS that axis.
    av_geom_set_stretch(true);

    mspacman_ports_wire(system);

    // The aux board powers up with the decrypted bank selected -- MAME's
    // init_mspacman() ends with `membank("bank1")->set_entry(1)`. This must
    // survive the memset above, hence its placement here rather than in the
    // struct's declared defaults. See mspacman_machine.h's `bank` comment
    // for what booting with the wrong value looks like (plain Pac-Man, not
    // a crash).
    system->bank = MSPACMAN_BANK_DECRYPTED;

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

bool mspacman_load_assets(mspacman_system *system, uint16_t *out_error_color) {
    mspacman_rom_load_status_t rom_status = mspacman_load_rom(system);
    if (rom_status == MSPACMAN_ROM_LOAD_NO_STORAGE) {
        *out_error_color = MSPACMAN_COLOR_ERROR_NO_CARD;
        return false;
    }
    if (rom_status == MSPACMAN_ROM_LOAD_NO_ROM_FILES) {
        *out_error_color = MSPACMAN_COLOR_ERROR_NO_ASSETS;
        return false;
    }

    mspacman_video_build_caches();
    hal_storage_unmount();

    hal_audio_init(MSPACMAN_AUDIO_SAMPLE_RATE);
    mspacman_audio_init(system);

    hal_input_init();
    return true;
}

// Runs the frame's cycles INTERLEAVED with scanline submission, evenly
// spreading MSPACMAN_CYCLES_PER_FRAME across the HAL_VIDEO_HEIGHT
// acquire/submit calls instead of running them all in one uninterrupted
// burst before the first call. Fixes arcade_arduino/DEVNOTES.md problem
// #19: even after fixing the renderer itself (problem #18), a real,
// visible stall remained because *this* loop still ran the whole frame's
// ~50,688 Z80 cycles before mspacman_draw_frame() ever called
// hal_video_acquire_scanline() -- same starvation mechanism, just moved
// from the renderer into the CPU loop.
//
// THIS IS NOW THE ONLY PATH. It used to be gated to tate/CW, with
// landscape/180 falling back to a fully sequential run_frame_sequential()
// because a yoko scanline needs a native COLUMN and the renderer could only
// produce rows. mspacman_video.cpp's render_native_column() removed that
// constraint, so the gate, the sequential path and the frame cache are all
// gone together -- and with them the red those orientations showed
// (DEVNOTES #79).
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
// "Compare elapsed delta, not absolute cyc" wraparound-safety -- see
// pacman_machine.cpp for the full explanation
// (arcade_arduino/DEVNOTES.md problem #22).
// `emulated_frames` DECOUPLES GAME SPEED FROM DISPLAY RATE, for boards
// whose display cannot sustain 60Hz. The machine advances that many frames
// of cycles and fires that many vblank interrupts while painting ONCE, so
// the Z80 sees the interrupt rate the real hardware produced and only the
// picture is decimated. See pacman_machine.cpp's copy of this for the full
// reasoning, the cost, and the one class of thing that must be scaled
// alongside it.
//
// THIS IS THE SECOND COPY. A THIRD SHOULD BE FACTORED OUT. The body is
// identical across machines apart from the cycle constant, the step
// function and the renderer, so a shared helper taking those three would
// remove the duplication -- but at two instances that is more machinery
// than it saves, and the machines deliberately do not share a frame loop
// today. If a third board-limited port appears, do it then.
static void run_frame_interleaved(mspacman_system *system,
                                  uint32_t emulated_frames) {
    if (emulated_frames < 1u) emulated_frames = 1u;
    const uint32_t total_cycles = MSPACMAN_CYCLES_PER_FRAME * emulated_frames;
    uint32_t start = system->cpu.cyc;
    uint32_t next_int = 1u;

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
        // has passed. The last is fired after the loop so it keeps its
        // original end-of-frame position exactly.
        while (next_int < emulated_frames &&
               target_delta >= MSPACMAN_CYCLES_PER_FRAME * next_int) {
            if (system->interrupt_enable) {
                z80_gen_int(&system->cpu, system->interrupt_vector);
            }
            next_int++;
        }

        uint16_t *buf = hal_video_acquire_scanline();
        mspacman_video_render_scanline(system, i, buf);
        hal_video_submit_scanline(buf);
    }

    // The final vblank, fired at the end -- unchanged from when this was
    // the only one.
    if (system->interrupt_enable) {
        z80_gen_int(&system->cpu, system->interrupt_vector);
    }
}

void mspacman_run_frame(mspacman_system *system) {
    // Every rotation, one path: mspacman_video.cpp's
    // render_native_column() removed the reason landscape/180 ever needed a
    // whole-frame burst (DEVNOTES #79).
    run_frame_interleaved(system, 1u);
}

void mspacman_run_frames(mspacman_system *system, uint32_t emulated_frames) {
    run_frame_interleaved(system, emulated_frames);
}
