// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

// i8080 port I/O for the Lunar Rescue machine.
// See lrescue_ports.cpp for the MAME source citations behind every bit.
#ifndef LRESCUE_PORTS_H
#define LRESCUE_PORTS_H

#include <stdint.h>
#include "lrescue_machine.h"

#ifdef __cplusplus
extern "C" {
#endif

// Binds this machine's port I/O to a specific system instance and installs
// it on that system's CPU (system->state.port_in / .port_out -- see the
// contract on Cpu_state in ArcadeCPU_i8080's i8080.h). Call once before
// running the CPU; until then IN reads 0xFF and OUT is discarded.
//
// The port handlers themselves are deliberately NOT declared here: they are
// file-static in lrescue_ports.cpp and reached only through those pointers.
// They used to be extern globals sharing exactly the names
// ArcadeMachine_Invaders defines, which was safe only while exactly one
// Machine library was ever linked into a sketch. That stopped being true,
// and the two definitions collided; see i8080.h for the full note.
void lrescue_ports_bind(arcade_system *system);

#ifdef __cplusplus
}
#endif

#endif
