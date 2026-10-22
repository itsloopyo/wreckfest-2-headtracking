// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

namespace wf2_ht::builds {

// One decoded x64 instruction, reduced to what the runtime discovery matches on.
// Registers are numbered 0..15 in encoding order (rax, rcx, rdx, rbx, rsp, rbp,
// rsi, rdi, r8..r15), with REX.R / REX.B / REX.X already folded in.
struct Insn {
    static constexpr int kNone = -1;
    static constexpr int kRip = 16;

    std::uint32_t rva = 0;
    std::uint8_t len = 0;
    // The first opcode byte, or 0x0F with opcode2 holding the second for the
    // two-byte map. VEX instructions report 0x0F and their opcode in opcode2.
    std::uint8_t opcode = 0;
    std::uint8_t opcode2 = 0;
    bool vex = false;
    bool rex_w = false;

    bool has_modrm = false;
    std::uint8_t mod = 0;
    std::uint8_t reg = 0;  // ModRM.reg, extended
    std::uint8_t rm = 0;   // ModRM.rm, extended; the register when mod == 3

    // Memory operand, when mod != 3.
    int base = kNone;
    int index = kNone;
    std::int64_t disp = 0;

    bool has_imm = false;
    std::int64_t imm = 0;  // sign-extended

    bool relative = false;
    std::uint32_t target = 0;  // branch target RVA when relative

    bool HasMemory() const { return has_modrm && mod != 3; }
    bool IsRipRelative() const { return HasMemory() && base == kRip; }
    std::uint32_t RipTarget() const {
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(rva) + len + disp);
    }
    // [base + disp] with no index register.
    bool IsMem(int base_reg) const { return HasMemory() && base == base_reg && index == kNone; }
};

// Decodes one instruction at `code`, which sits at `rva` and has `avail`
// readable bytes. Returns false for anything the decoder cannot length, which
// includes EVEX; callers treat that as the end of what they can trust.
bool DecodeInsn(const std::uint8_t* code, std::size_t avail, std::uint32_t rva, Insn& out);

}  // namespace wf2_ht::builds
