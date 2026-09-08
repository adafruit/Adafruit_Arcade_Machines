// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// i8080 port I/O for the Space Invaders machine.
// Ported from invaders_pico's i8080_ports.c (itself identical to
// shotto42/invaders except for the sound backend call).
#ifndef INVADERS_PORTS_H
#define INVADERS_PORTS_H

#include <stdint.h>
#include "invaders_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

// Binds this machine's port I/O to a specific system instance and installs
// it on that system's CPU (system->state.port_in / .port_out -- see the
// contract on Cpu_state in ArcadeCPU_i8080's i8080.h). Call once before
// running the CPU; until then IN reads 0xFF and OUT is discarded.
//
// The port handlers themselves are deliberately NOT declared here: they are
// file-static in invaders_ports.cpp and reached only through those pointers,
// so two i8080 machines can be compiled into one build without colliding.
void invaders_ports_bind(arcade_system *system);

#ifdef __cplusplus
}
#endif

#endif
