// SPDX-FileCopyrightText: 2020 Ingrid Rebecca Abraham
// SPDX-FileCopyrightText: 2024 Stephan Hotto
// SPDX-FileCopyrightText: 2026 John Park for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

enum Register {
    A,
    B,
    C,
    D,
    E,
    H,
    L,
    SP,
    M,
    PSW,
};

typedef struct {
    bool z;
    bool s;
    bool p;
    bool cy;
    bool ac;
} Condition_codes;

typedef struct Cpu_state Cpu_state;

// Machine-supplied I/O for the IN/OUT opcodes. See the port_in/port_out
// fields on Cpu_state below for the contract these implement.
typedef uint8_t (*i8080_port_in_fn)(Cpu_state *state, uint8_t port_number);
typedef void    (*i8080_port_out_fn)(Cpu_state *state, uint8_t port_number,
                                     uint8_t port_data);

struct Cpu_state {
    uint8_t regs[7];
    uint16_t sp;
    uint16_t pc;
    uint8_t memory[0x10000]; // full 16-bit address space -- individual machines
                             // only populate the ranges their own board actually
                             // decodes ROM/RAM into (Space Invaders uses 0x4000
                             // of it; other 8080bw-family boards, e.g. Lunar
                             // Rescue, use more, and not always contiguously).
    Condition_codes cc;
    uint8_t int_enable;
    // Some 8080bw-family boards incompletely decode their address bus, aliasing
    // RAM at 0x2000-0x3fff onto 0x4000-0x5fff (Space Invaders' original PCB
    // does this, and its self-test code relies on it). Other boards in the same
    // family map real ROM at 0x4000 instead (Lunar Rescue's extra two chips
    // live at 0x4000-0x4fff) -- for those, this must stay false. Set true only
    // for a machine that has confirmed it needs the alias; see read_memory()/
    // write_memory() in i8080.c.
    bool mirror_2000_at_4000;

    // Machine-supplied port I/O, called by the IN and OUT opcodes. This is
    // the whole "CPU axis" hardware contract -- memory is handled inside the
    // core (read_memory()/write_memory() take a Cpu_state *), so ports are
    // the only hook a Machine library has to fill in, and there is no
    // userdata field here for that reason: unlike ArcadeCPU_Z80, which also
    // delegates memory and so must carry one, a machine here reaches its own
    // state from the translation unit that installs these.
    //
    // These were extern globals named read_port()/write_port() until the
    // single-library restructure. That worked only because exactly one
    // Machine library was ever linked into a sketch; the moment two i8080
    // machines (Space Invaders and Lunar Rescue) shared one build, their two
    // definitions collided at link time. Function pointers also match the
    // shape ArcadeCPU_Z80, _M6502 and _MCS48 already use.
    //
    // Install both before running the CPU -- see invaders_ports_bind() in
    // ArcadeMachine_Invaders for the reference implementation. Leaving either
    // NULL is not an error: IN then reads 0xFF (an undriven bus) and OUT is
    // discarded, which keeps an unbound machine debuggable instead of
    // faulting a core that has no debugger attached.
    i8080_port_in_fn  port_in;
    i8080_port_out_fn port_out;
};

// i8080.c is compiled as plain C. (This header used to be shared verbatim
// with the invaders_pico reference clone, a pure-C project; the port_in/
// port_out fields above are the first deliberate divergence, so diffing the
// two files no longer expects an exact match.) extern "C" here is only
// for C++ callers (e.g. ArcadeMachine_Invaders's .cpp files) -- without it,
// a C++ translation unit would mangle calls to these and fail to link
// against i8080.c's plain C symbols.
#ifdef __cplusplus
extern "C" {
#endif

uint8_t read_memory(Cpu_state *state, uint16_t address);
void write_memory(Cpu_state *state, uint16_t address, uint8_t value);
int interrupt(Cpu_state *state, uint16_t offset);
int exec_opcode(Cpu_state *state);

#ifdef __cplusplus
}
#endif

#endif
