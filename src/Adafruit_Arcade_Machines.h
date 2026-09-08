// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// Adafruit Arcade Machines -- umbrella header.
//
// Every sketch includes this FIRST. Two reasons, one of them structural:
//
//  1. It is how the Arduino builder finds this library at all. Library
//     resolution matches an #include against the headers at the top of a
//     library's src/, so a path-qualified include like
//     <machines/pacman/pacman_machine.h> does NOT pull the library into the
//     build on its own -- it only resolves once something has. Include this
//     header first and the rest of src/ becomes reachable by path.
//  2. It carries the optimisation guard below, which is the single most
//     expensive mistake this project has to defend against.
//
// It deliberately pulls in ONLY the board-agnostic HAL contracts. A sketch
// names its own board and machine explicitly:
//
//     #include <Adafruit_Arcade_Machines.h>
//     #include <boards/fruitjam/board_config_fruitjam.h>
//     #include <machines/invaders/invaders_machine.h>
//
// That keeps SAMP's axes visible in the one file that is allowed to know
// both "this game" and "this board" -- see README.md.
#ifndef ADAFRUIT_ARCADE_MACHINES_H
#define ADAFRUIT_ARCADE_MACHINES_H

// -----------------------------------------------------------------------
// Optimisation guard. Do not remove without reading DEVNOTES.md #35/#49.
//
// The RP2350 core's DEFAULT is "Small (-Os) (standard)", and no game here
// is fast enough at it: the emulated frame overruns the 16.66ms DVI budget,
// the scanline queue starves, and the screen goes SOLID RED -- the same red
// this project uses for "no SD card", which is why the mistake is so
// expensive to diagnose. Ms. Pac-Man needs 19.5ms at -Os and Galaga 17.8ms.
//
// Each example's sketch.yaml pins the right level and arduino-cli honours
// it, but the Arduino IDE does not always, and opening an example from
// File > Examples uses whatever the board menu currently says. Failing the
// build with a sentence beats shipping a red screen that looks like broken
// hardware.
//
// -O0 and -Og are not caught here (they define no __OPTIMIZE_SIZE__) and
// are also too slow; they are not a default anyone lands on by accident.
// -----------------------------------------------------------------------
#if defined(__OPTIMIZE_SIZE__)
#error "Adafruit Arcade Machines: set Tools > Optimize to 'Optimize More (-O2)' or \
'Optimize Even More (-O3)'. The default -Os misses the 16.66ms frame budget \
and the screen goes solid red, which looks exactly like a hardware fault. \
See each example's sketch.yaml for the level that example needs."
#endif

#include "hal/ArcadeHAL.h"          // the four video/audio/input/storage contracts
#include "hal/arcade_video_geom.h"  // rotation / mirror / aspect-correction geometry

#endif
