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
// Optimisation guard. Do not remove without reading extras/DEVNOTES.md
// #35/#49.
//
// On RP2040/RP2350 this is a hard error, because it is measured: at the
// core's default "Small (-Os) (standard)" no game here fits the 16.66ms DVI
// frame budget. The scanline queue starves and the screen goes SOLID RED --
// the same red this project uses for "no SD card", which is why the mistake
// is so expensive to diagnose. Ms. Pac-Man needs 19.5ms at -Os, Galaga
// 17.8ms.
//
// Elsewhere it is only a warning. These are cycle-accurate emulators and
// -Os is very unlikely to be fast enough on any target, but "unlikely" is
// not the same as measured, and a hard error would mean a new architecture
// cannot even be compiled to find out. The ESP32 core defaults to -Os, so
// erroring there would break the first build of every port before anyone
// had a number. Raise it to an error for a platform once someone has one.
// -----------------------------------------------------------------------
#if defined(__OPTIMIZE_SIZE__)
  #if defined(ARDUINO_ARCH_RP2040) || defined(PICO_ON_DEVICE)
    #error "Adafruit Arcade Machines: set Tools > Optimize to 'Optimize More (-O2)' or \
'Optimize Even More (-O3)'. The default -Os misses the 16.66ms frame budget \
and the screen goes solid red, which looks exactly like a hardware fault. \
See each example's sketch.yaml for the level that example needs."
  #else
    #warning "Adafruit Arcade Machines is being built at -Os. These are \
cycle-accurate emulators; on RP2 that is measurably too slow to hold 60fps, \
and it is unlikely to be fast enough here either. Raise Tools > Optimize if \
the frame rate disappoints."
  #endif
#endif

#include "hal/ArcadeHAL.h"          // the four video/audio/input/storage contracts
#include "hal/arcade_video_geom.h"  // rotation / mirror / aspect-correction geometry

#endif
